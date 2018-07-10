#include "shared.h"
#include <time.h>

#define NUM_READ_THREAD 1
#define NUM_WRITE_THREAD 2
#define FILE_SIZE 64
#define READ_CHAR 10
#define LOOP 10

static char file0[32];
static long num_cores;

void *start_read(void *ptr) {
  int f0, ret, i, j, pos;
  int *id = (int *) ptr;
  ssize_t ret2;
  char *buf = (char *) malloc(READ_CHAR);
  pid_t pid = syscall(__NR_gettid);

  bind_core((*id) % num_cores);

  printf("read pthread started, current process ID : %d\n", pid);

  f0 = open(file0, O_RDWR, 0644);
  if (f0 < 0) printf("\tError: open() ret = %d\n", f0);

  for (j = 0; j < LOOP; j++) {
    ret = syscall(322);
    if (ret < 0) perror("\tread fs_txbegin");

    for (i = 0; i < READ_CHAR; i ++) {
      pos = rand() % FILE_SIZE;
      lseek(f0, pos, SEEK_SET);
      ret2 = read(f0, &buf[i], 1);
      if (ret2 < 0) perror("\tread");
    }

    if (ret2 >= 0) printf("Thread %d read: %s\n", *id, buf);

    ret = syscall(323);
    if (ret < 0) perror("\tread fs_txend");
    usleep(10);
  }

  close(f0);
  free(buf);
  free(ptr);
}

void *start_write(void *ptr) {
  int f0, i, j, ret;
  int *id = (int *) ptr;
  ssize_t ret2;
  char buf;
  pid_t pid = syscall(__NR_gettid);

  bind_core((*id + NUM_READ_THREAD) % num_cores);

  printf("write pthread started, current process ID : %d\n", pid);
  buf = (char) ((*id) % (90 - 65 + 1) + 65);

  f0 = open(file0, O_RDWR, 0644);
  if (f0 < 0) perror("\topen");

  for (j = 0; j < LOOP; j++) {
    lseek(f0, 0, SEEK_SET);
    ret = syscall(322);
    if (ret < 0) perror("\twrite fs_txbegin");

    for (i = 0; i < FILE_SIZE; i++) {
      ret2 = write(f0, &buf, sizeof(char));
      if (ret2 < 0) perror("\twrite");
    }

    ret = syscall(323);
    if (ret < 0) perror("\twrite fs_txend");
    usleep(10);
  }

  fsync(f0);

  close(f0);
  free(ptr);
}

int main(void) {
  int ret, i, fd;
  char cmd[80];
  pthread_t read_threads[NUM_READ_THREAD];
  pthread_t write_threads[NUM_WRITE_THREAD];

  num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  sprintf(file0, "%s/file0", PATH_VALUE(TESTDIR));
  sprintf(cmd, "rm -f %s", file0);
  system(cmd);
  sprintf(cmd, "touch %s", file0);
  system(cmd);
  sprintf(cmd, "dd if=/dev/zero bs=%d count=1 | tr \"\\000\" \"\\060\"  >%s", FILE_SIZE, file0);
  system(cmd);
  srand(time(NULL));

  syscall(324);

  for (i = 0; i < NUM_READ_THREAD; i ++) {
    int *arg = malloc(sizeof(*arg));
    *arg = i;
    ret = pthread_create(&read_threads[i], NULL, start_read, arg);

    if (ret != 0) printf("Error starting thread %d\n", i);
  }

  for (i = 0; i < NUM_WRITE_THREAD; i ++) {
    int *arg = malloc(sizeof(*arg));
    *arg = i;
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

  syscall(325);
}
