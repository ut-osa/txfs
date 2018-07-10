#include "shared.h"

#include <signal.h>
#include <sys/mman.h>

typedef struct {
  int id;
  size_t data_size;
  char* mem;
  char* data;
} pthread_arg_t;

static pthread_mutex_t sys_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t inc_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t barrier;
static int num_conflicts = 0;
static int num_successes = 0;

int is_conflict(int rc, int err) {
  return rc == -1 && err == ECONFLICT;
}

static void* routine(void* arg) {
  pthread_arg_t* a = (pthread_arg_t*) arg;
  int rc = 0, id = a->id, data_size = a->data_size;
  char* data = a->data;
  char* mem = a->mem;
  timer_t timer = {0};
  sigevent_t se = {0};
  struct itimerspec duration = {
    .it_interval = {0},
    .it_value = {
      .tv_sec = 10,
      .tv_nsec = 0
    }
  };

	// Lock CPU.
	bind_core(id);

  // Phase one: Start transaction, make changes.
  rc = syscall(TX_BEGIN);
  if (rc < 0) {
    fprintf(stderr, "[%d] Error in tx_begin: %d\n", id, errno);
    goto b_error;
  }

  memcpy(mem, data, data_size);
  rc = msync(mem, data_size, MS_SYNC);
  if (rc < -1) {
    fprintf(stderr, "[%d] Error in msync: %d\n", id, errno);
    goto b_error;
  }

  // Wait to synchronize - if this takes too long, a thread has already died
  // and we should abort.
  se.sigev_notify = SIGEV_SIGNAL;
  se.sigev_signo = SIGKILL;
  rc = timer_create(CLOCK_REALTIME, &se, &timer);
  if (rc) {
    fprintf(stderr, "[%d] Error creating timer\n", id);
    goto b_error;
  }
  rc = timer_settime(timer, 0, &duration, NULL);
  if (rc) {
    fprintf(stderr, "[%d] Error setting timer\n", id);
    goto b_error;
  }
  //fprintf(stdout, "[%d] Waiting...\n", id);
  pthread_barrier_wait(&barrier);
  //fprintf(stdout, "[%d] Resumed.\n", id);
  timer_delete(timer);

  // Phase two: Try to commit the conflicting transaction.
  rc = syscall(TX_COMMIT);
  //fprintf(stdout, "[%d] tx_commit completed.\n", id);
  if (is_conflict(rc, errno)) {
    goto conflict;
  } else if (rc) {
    fprintf(stderr, "tx_commit failed: %s (%d)\n", strerror(errno), errno);
    goto error;
  } else {
    // Confirm that the successful thread's data is the data that exists in
    // memory.
    if(strcmp(mem, data)) {
      fprintf(stderr, "Expected data: \'%s\', actual data: \'%s\'\n",
          data, mem);
      goto error;
    }
    pthread_mutex_lock(&inc_lock);
    num_successes++;
    pthread_mutex_unlock(&inc_lock);
  }
  return 0;

b_error:
  // If there is an early error, we need to still wait at the barrier so all
  // the other threads can meet up.
  pthread_barrier_wait(&barrier);
error:
  syscall(TX_ABORT);
  return (void*)-1;

b_conflict:
  // If there is an early conflict, we need to still wait at the barrier so all
  // the other threads can meet up.
  pthread_barrier_wait(&barrier);
conflict:
  syscall(TX_ABORT);
  pthread_mutex_lock(&inc_lock);
  num_conflicts++;
  pthread_mutex_unlock(&inc_lock);
  return 0;
}

void kill_main(union sigval _unused) {
  fprintf(stderr, "Timeout exceeded without all threads finishing.\n");
  exit(-1);
}

int main(int argc, const char** argv) {
  int rc = 0, join_rc = 0, i = 0, num_threads = 0, fd = 0, size = 1024;
  char *mem = NULL;
  pthread_t* pthread_arr = NULL;
  pthread_arg_t* args = NULL;
  timer_t timer = {0};
  sigevent_t se = {
    .sigev_notify = SIGEV_THREAD,
    .sigev_notify_function = kill_main
  };
  struct itimerspec duration = {
    .it_interval = {0},
    .it_value = {
      .tv_sec = 10,
      .tv_nsec = 0
    }
  };
	FILE_DEFINE(0);

  // Parse args.
  if (argc < 2) {
    printf("Usage: ./conflict-microbench <number of threads>\n");
    return -1;
  }
  num_threads = atoi(argv[1]);
  duration.it_value.tv_sec *= num_threads;

  // Set up memory.
	// -- Open whatever file we happen to be using.
	fd = open(file0, O_RDWR | O_CREAT, 0777);
	if (fd < 0) {
		fprintf(stderr, "Could not open file \'%s\': %s.\n", file0,
				strerror(errno));
		return -1;
	}
	// -- Stretch our new file to the appropriate size.
	rc = ftruncate(fd, size);
	if (rc) {
		fprintf(stderr, "Could not resize file: %s.\n", strerror(errno));
		return -1;
	}
  // -- Make sure our changes have propagated.
  rc = fsync(fd);
	if (rc) {
		fprintf(stderr, "Could not fsync after resize: %s.\n", strerror(errno));
		return -1;
	}
	// -- Map some memory as file-backed.
	mem = (char*) mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED) {
		fprintf(stderr, "Could not mmap file-backed region: %s.\n",
				strerror(errno));
		return -1;
	}

  // pthread initializations.
  rc = pthread_barrier_init(&barrier, NULL, num_threads);
  if (rc) {
    fprintf(stderr, "Error initializing barrier: %s\n", strerror(errno));
		return -1;
  }

  pthread_arr = (pthread_t*) calloc(num_threads, sizeof(pthread_t));
	if (!pthread_arr) {
		fprintf(stderr, "Could not calloc pthread structs: %s.\n",
				strerror(errno));
		return -1;
	}
  args = (pthread_arg_t*) calloc(num_threads, sizeof(pthread_arg_t));
  if (!args) {
		fprintf(stderr, "Could not calloc pthread arg structs: %s.\n",
				strerror(errno));
    return -1;
  }

  // Start threads.
  syscall(TX_DEBUG_BEGIN);
  for (i = 0; i < num_threads; i++) {
    args[i].mem = mem;
    args[i].id = i;
    args[i].data = strdup("X");
    if (!args[i].data) {
      fprintf(stderr, "Error with strdup: %s.\n", strerror(errno));
      return -1;
    }
    args[i].data[0] = 'A' + (char)i;
    args[i].data_size = strlen(args[i].data);
    rc = pthread_create(&pthread_arr[i], NULL, routine, (void*)&args[i]);
    if (rc) {
      fprintf(stderr, "Error creating thread %d: %s.\n", i, strerror(errno));
      return -1;
    }
  }

  // Create a timer in case one of the threads hangs.
  rc = timer_create(CLOCK_REALTIME, &se, &timer);
  if (rc) {
    fprintf(stderr, "Error creating timer in main: %s.\n", strerror(errno));
    return -1;
  }
  rc = timer_settime(timer, 0, &duration, NULL);
  if (rc) {
    fprintf(stderr, "Error setting timer in main: %s.\n", strerror(errno));
    return -1;
  }

  // Wait for all threads to finish.
  for (i = 0; i < num_threads; i++) {
    rc = pthread_join(pthread_arr[i], (void**)&join_rc);
    if (rc) {
      fprintf(stderr, "Error joining on thread %d: %s (%d)\n", i,
          strerror(rc), rc);
      return -1;
    }
    if (join_rc) {
      fprintf(stderr, "Error in thread %d: rc = %d\n", i, join_rc);
      return -1;
    }
  }
  rc = timer_delete(timer);
	if (rc) {
		fprintf(stderr, "Error deleting timer in main: %s.\n", strerror(errno));
		return -1;
	}
  // Confirm we got the correct number of conflicts; 1 less than number of
  // threads.
  if (num_conflicts != num_threads - 1) {
    fprintf(stderr, "Error: expected conflicts: %d, actual: %d\n",
       num_threads - 1, num_conflicts);
    return -1;
  } else if (num_successes != 1) {
    fprintf(stderr, "Error: expected successes: %d, actual: %d\n",
       1, num_successes);
    return -1;
  } else {
    fprintf(stdout, "Microbench completed successfully.\n"
                    "\tConflicts: %d\n\tSuccesses: %d\n", num_conflicts,
                    num_successes);
  }
  syscall(TX_DEBUG_END);
  return 0;
}
