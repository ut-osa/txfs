#include "shared.h"

#define NUM_READ_THREAD 10
#define NUM_WRITE_THREAD 3

static char file0[32];
static long num_cores;

void *start_read(void *ptr) {
  int f0, ret;
  ssize_t ret2;
  char buf[8];
  int *id = (int *) ptr;
  pid_t pid = syscall(__NR_gettid);

  bind_core((*id) % num_cores);

  printf("read pthread started, current process ID : %d\n", pid);

  f0 = open(file0, O_CREAT | O_RDWR, 0644);
  if (f0 < 0) printf("\tError: open() ret = %d\n", f0);

  ret = syscall(322);
  if (ret < 0) printf("\tError: write sys_txbegin().\n");

  lseek(f0, (*id) * 8, SEEK_SET);
  ret2 = read(f0, buf, 8);
  if (ret2 < 0) printf("\tError: read ret = %d\n", (int) ret2);
  else printf("Thread %d Read: %s", *id, buf);

//  ret = syscall(323);
//  if (ret < 0) printf("\tError: read sys_txend().\n");

  close(f0);
  free(ptr);
}

void *start_write(void *ptr) {
  int f0, ret;
  int *id = (int *) ptr;
  ssize_t ret2;
  pid_t pid = syscall(__NR_gettid);

  bind_core((*id + NUM_READ_THREAD) % num_cores);

  printf("write pthread started, current process ID : %d\n", pid);

  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  if (f0 < 0) printf("\tError: open() ret = %d\n", f0);

  ret = syscall(322);
  if (ret < 0) printf("\tError: write sys_txbegin().\n");

  ret2 = write(f0, "0000000\n", 8);
  if (ret2 < 0) printf("\tError: write ret = %d\n", (int) ret2);

  ret = syscall(323);
  if (ret < 0) printf("\tError: write sys_txend().\n");

  close(f0);
  free(ptr);
}

int main(void) {
  int ret, i;
  char buf[8];
  pthread_t read_threads[NUM_READ_THREAD];
  pthread_t write_threads[NUM_WRITE_THREAD];

  sprintf(file0, "%s/file0", PATH_VALUE(TESTDIR));

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

//  syscall(325);
}
