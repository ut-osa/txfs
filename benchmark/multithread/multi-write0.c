#include "shared.h"

#define NUM_THREAD 10

static char file0[32];
static long num_cores;

void *start(void *ptr) {
  int f0, ret;
  int *id = (int *) ptr;
  ssize_t ret2;
  pid_t pid = syscall(__NR_gettid);

  bind_core((*id) % num_cores);

  printf("pthread started, current process ID : %d\n", pid);

  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  if (f0 < 0) printf("Error: open() ret = %d\n", f0);

  ret = syscall(322);
  if (ret < 0) printf("\tError: sys_txbegin().\n");

  ret2 = write(f0, "0000000\n", 8);
  if (ret2 < 0) printf("\tError: write ret = %d\n", (int) ret2);

  ret = syscall(323);
  if (ret < 0) perror("\tError sys_txend(): ");

  close(f0);
  free(ptr);
}

int main(void) {
  int ret, i;
  char buf[8];
  pthread_t threads[NUM_THREAD];

  num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  sprintf(file0, "%s/file0", PATH_VALUE(TESTDIR));

  syscall(324);

  for (i = 0; i < NUM_THREAD; i ++) {
	  int *arg = malloc(sizeof(*arg));
	  *arg = i;
	  ret = pthread_create(&threads[i], NULL, start, arg);

	  if (ret != 0) printf("Error starting thread %d\n", i);
  }

  for (i = 0; i < NUM_THREAD; i ++) {
	  ret = pthread_join(threads[i], NULL);
	  printf("pthread %d finished\n", i);

	  if (ret != 0) printf("Error joining thread %d\n", i);
  }

  syscall(325);
}
