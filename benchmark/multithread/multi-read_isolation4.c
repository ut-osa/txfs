#include "shared.h"
#include <time.h>

#define NUM_READ_THREAD 1
#define NUM_WRITE_THREAD 1
#define FILE_SIZE 64
#define READ_CHAR 10
#define LOOP_READ 20
#define LOOP_WRITE 5

struct input_args {
  int id;
  int transactional;
  int fd;
};

static char file0[32];
static long num_cores;

/* Parent thread shares file descriptor with child threads.
 * This test checks whether transactional or non-transactional reads never read
 * pages from a transactional write of another thread. Non-transactional reads
 * get the global data, while transactional reads return -ECONFLICT. */

/* Transactional reads. */
void *start_read(void *ptr) {
  int ret, i, j, pos;
  struct input_args *input = (struct input_args*) ptr;
  ssize_t ret2;
  char *buf = (char *) malloc(READ_CHAR);
  pid_t pid = syscall(__NR_gettid);

  bind_core(input->id % num_cores);

  printf("read pthread started, current process ID : %d\n", pid);

  for (j = 0; j < LOOP_READ; j++) {
    if (input->transactional) {
      ret = syscall(322);
      if (ret < 0) perror("\tread fs_txbegin");
    }

    for (i = 0; i < READ_CHAR; i ++) {
      pos = rand() % FILE_SIZE;
      lseek(input->fd, pos, SEEK_SET);
      ret2 = read(input->fd, &buf[i], 1);
      if (ret2 < 0) perror("\tread");
    }

    if (ret2 >= 0) printf("Thread %d read: %s\n", input->id, buf);

    if (input->transactional) {
      ret = syscall(323);
      if (ret < 0) perror("\tread fs_txend");
    }
    usleep(1000000);
  }

  free(buf);
  free(ptr);
}

/* Transactional writes. */
void *start_write(void *ptr) {
  struct input_args *input = (struct input_args*) ptr;
  int i, j, ret;
  int *id = (int *) ptr;
  ssize_t ret2;
  char buf;
  pid_t pid = syscall(__NR_gettid);

  bind_core((*id + NUM_READ_THREAD) % num_cores);

  printf("write pthread started, current process ID : %d\n", pid);

  for (j = 0; j < LOOP_WRITE; j++) {
    buf = (char) ((*id) % (90 - 65 + 1) + 65 + j);
    lseek(input->fd, 0, SEEK_SET);
    ret = syscall(322);
    printf("Enter transactional write.\n");
    if (ret < 0) perror("\twrite fs_txbegin");

    for (i = 0; i < FILE_SIZE; i++) {
      ret2 = write(input->fd, &buf, sizeof(char));
      if (ret2 < 0) perror("\twrite");
    }
    getchar();
    printf("Committing write.\n");
    ret = syscall(323);
    if (ret < 0) perror("\twrite fs_txend");
    getchar();
  }

  fsync(input->fd);
  free(ptr);
}

int main(int argc, char* const argv[]) {
  int ret, i, c, fd, f0;
  char cmd[80];
  int is_transactional = 0;
  pthread_t read_threads[NUM_READ_THREAD];
  pthread_t write_threads[NUM_WRITE_THREAD];

  num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  while ( (c = getopt(argc, argv, "th")) != -1 ){
    switch(c) {
      case 't':
        is_transactional = 1;
        break;
      case 'h':
        printf("Usage: %s [-t (Use transactional reads)] \n"
               "[-h prints this message] \n",
	       argv[0]);
        return 0;
      case '?':
        fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
        return -1;
    }
  }

  sprintf(file0, "%s/file0", PATH_VALUE(TESTDIR));
  sprintf(cmd, "rm -f %s", file0);
  system(cmd);
  sprintf(cmd, "touch %s", file0);
  system(cmd);
  sprintf(cmd, "dd if=/dev/zero bs=%d count=1 | tr \"\\000\" \"\\060\"  >%s", FILE_SIZE, file0);
  system(cmd);
  srand(time(NULL));

  f0 = open(file0, O_RDWR, 0644);
  if (f0 < 0) printf("\tError: open() ret = %d\n", f0);

  syscall(324);

  for (i = 0; i < NUM_READ_THREAD; i ++) {
    struct input_args *arg = malloc(sizeof(*arg));
    arg->id;
    arg->transactional = is_transactional;
    arg->fd = f0;
    ret = pthread_create(&read_threads[i], NULL, start_read, arg);

    if (ret != 0) printf("Error starting thread %d\n", i);
  }

  for (i = 0; i < NUM_WRITE_THREAD; i ++) {
    struct input_args *arg = malloc(sizeof(*arg));
    arg->id;
    arg->transactional = is_transactional;
    arg->fd = f0;
    ret = pthread_create(&write_threads[i], NULL, start_write, arg);

    if (ret != 0) printf("Error starting thread %d\n", i);
  }

  for (i = 0; i < NUM_READ_THREAD; i ++) {
    ret = pthread_join(read_threads[i], NULL);
    printf("read pthread %d finished\n", i);

    if (ret != 0) printf("Error joining read thread %d\n", i);
  }

  for (i = 0; i < NUM_WRITE_THREAD; i ++) {
	  ret = pthread_join(write_threads[i], NULL);
	  printf("write pthread %d finished\n", i);

	  if (ret != 0) printf("Error joining write thread %d\n", i);
  }

  close(f0);

  syscall(325);
}
