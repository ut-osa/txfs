#include "shared.h"

#include <time.h>

#define NUM_FILES 1000
#define NUM_ENTRIES (NUM_FILES + 2)
#define NUM_ITER 1000

char dir[1024];

static long num_threads;

typedef struct {
  int thread_id;
} arg_t;

void* list_dir(void* argptr) {
  int fd = 0, ret = 0, retries = 0, num = 10, bpos, iter = 0;
  char dent_buf[DENT_BUF_SIZE];
  struct linux_dirent64 *d;
  int num_entries = 0;
  int thread_num = ((arg_t*)argptr)->thread_id;

  bind_core(thread_num % num_threads);

  system("echo 3 > /proc/sys/vm/drop_caches");

  while (iter++ < NUM_ITER) {
tx:
    num_entries = 0;
    ret = syscall(TX_BEGIN);
    if (ret) {
      perror("\tfs_txbegin");
      return (void*)-1;
    }

    fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
      syscall(TX_ABORT);
      if (errno == ECONFLICT) {
        goto tx;
      } else {
        perror("\t\topen");
        return (void*)-1;
      }
    }

    ret = 1;
    while (ret != 0) {
      // Read entries.
      ret = getdents64(fd, dent_buf, num * sizeof(struct linux_dirent64));
      if (ret < 0) {
        syscall(TX_ABORT);
        if (errno == ECONFLICT) {
          goto tx;
        } else {
          fprintf(stderr, "getdents64: %d (%s)\n", errno, strerror(errno));
          return (void*)-1;
        }
      }
      // Count entries.
      for (int bpos = 0; bpos < ret;) {
        d = (struct linux_dirent64 *) (dent_buf + bpos);
        bpos += d->d_reclen;
        ++num_entries;
      }
    }

    ret = syscall(TX_COMMIT);
    if (ret < 0 && errno == ECONFLICT && retries < MAX_RETRIES) {
      retries++;
      goto tx;
    } else if (ret < 0 && retries >= MAX_RETRIES) {
      perror("\tnumber of retries exceeded");
      return (void*)-1;
    } else if (ret < 0) {
      perror("\tTX_COMMIT");
      return (void*)-1;
    }

    close(fd);

    if (num_entries != NUM_ENTRIES) {
      fprintf(stderr, "Error: num entries found = %d/%d\n", num_entries,
          NUM_ENTRIES);
      return (void*)-1;
    }
  }

  return (void*)0;
}

int main(void) {
  char cmd[1024];
  int ret = 0, i, f, pthread_ret, pthreads_failed;
  pthread_t* threads;

  num_threads = sysconf(_SC_NPROCESSORS_ONLN);

  threads = (pthread_t*) malloc(num_threads * sizeof(*threads));

  snprintf(dir, 1023, "%s/testdirlist", PATH_VALUE(TESTDIR));
  snprintf(cmd, 1023, "mkdir -p %s", dir);
  ret = system(cmd);
  if (ret) {
    perror("Make test directory:");
    return ret;
  }

  for (f = 0; f < NUM_FILES; ++f) {
    snprintf(cmd, 1023, "echo %x > %s/file%d.txt", f, dir, f);
    ret = system(cmd);
    if (ret) {
      perror("Make test files:");
      return ret;
    }
  }

  sync();

  srand(time(NULL));

  printf("Setup complete.\n");

  syscall(TX_DEBUG_BEGIN);
  /* Start threads. */
  for (i = 0; i < num_threads; i++) {
    arg_t *arg = malloc(sizeof(arg_t));
    if (!arg) {
      fprintf(stderr, "Could not malloc arg_t: %s.\n", strerror(errno));
      return -1;
    }

    arg->thread_id = i;
    ret = pthread_create(&(threads[i]), NULL, list_dir, arg);

    if (ret) {
      fprintf(stderr, "Error starting thread %d: %s\n", i, strerror(errno));
      return -1;
    }
  }
  printf("pthread startup complete.\n");

  // Randomly modify some files
  for (int n = 0; n < NUM_ITER; ++n) {
    snprintf(cmd, 1023, "%s/file%d.txt", dir, n);
    printf("Modify %s/file%d.txt ....\n", dir, n);
    syscall(TX_BEGIN);
    int fd = open(cmd, O_RDWR);
    if (fd < 0) {
      if (errno == ECONFLICT) {
        syscall(TX_ABORT);
        --n;
        continue;
      } else {
        perror("File open:");
        break;
      }
    }

    ret = write(fd, "\nMODIFY", 8);
    if (ret < 8) {
      if (errno == ECONFLICT) {
        syscall(TX_ABORT);
        --n;
        continue;
      } else {
        perror("File write:");
        break;
      }
    }

    ret = close(fd);
    if (ret < 0) {
      if (errno == ECONFLICT) {
        syscall(TX_ABORT);
        --n;
        continue;
      } else {
        perror("File close:");
        break;
      }
    }

    ret = syscall(TX_COMMIT);
    if (ret < 0) {
      if (errno == ECONFLICT) {
        syscall(TX_ABORT);
        --n;
        continue;
      } else {
        perror("TXFS Commit:");
        break;
      }
    }
  }

  printf("Modify complete.\n");

  /* Wait for threads to stop. */
  for (i = 0; i < num_threads; i++) {
    pthread_ret = 0;
    ret = pthread_join(threads[i], (void**)(&pthread_ret));

    if (ret) {
      fprintf(stderr, "Error joining thread %d (ret = %d): %s\n", i, ret,
          strerror(errno));
    } else {
      printf("pthread %d finished\n", i);
    }

    if (pthread_ret) {
      pthreads_failed = 1;
    }
  }
  syscall(TX_DEBUG_END);

  printf("Benchmark complete.\n");

  return pthreads_failed;
}
