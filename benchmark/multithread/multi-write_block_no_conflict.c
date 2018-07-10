#include "shared.h"

#define NUM_WRITE_THREAD 5
#define BUF_SIZE 8

static char file0[32];
static long num_cores;

void *start_write(void *ptr) {
  int f0, i, ret;
  int *id = (int *) ptr;
  ssize_t ret2;
  char ch, buf[8];
  pid_t pid = syscall(__NR_gettid);

  bind_core((*id) % num_cores);

  printf("write pthread started, current process ID : %d\n", pid);
  ch = (char) ((*id) % (90 - 65 + 1) + 65);
  for (i = 0; i < BUF_SIZE; i ++) buf[i] = ch;

  f0 = open(file0, O_CREAT | O_RDWR, 0644);
  if (f0 < 0) perror("\topen");

  ret = syscall(322);
  if (ret < 0) perror("\twrite fs_txbegin");

  lseek(f0, (*id) * 4096, SEEK_SET);
  ret2 = write(f0, buf, BUF_SIZE);
  if (ret2 < 0) perror("\twrite");

  ret = syscall(323);
  if (ret < 0) perror("\twrite fs_txend");

  close(f0);
  free(ptr);
}

int main(void) {
  int ret, i;
  char cmd[80];
  pthread_t write_threads[NUM_WRITE_THREAD];

  num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  sprintf(file0, "%s/file0", PATH_VALUE(TESTDIR));
  sprintf(cmd, "rm -f %s", file0);
  system(cmd);
  sprintf(cmd, "touch %s", file0);
  system(cmd);
  sprintf(cmd, "dd if=/dev/zero bs=%d count=1 | tr \"\\000\" \"\\060\"  >file0",
      4096 * NUM_WRITE_THREAD);
  system(cmd);

  syscall(324);

  for (i = 0; i < NUM_WRITE_THREAD; i ++) {
    int *arg = malloc(sizeof(*arg));
    *arg = i;
    ret = pthread_create(&write_threads[i], NULL, start_write, arg);

    if (ret != 0) printf("Error starting thread %d\n", i);
  }

  for (i = 0; i < NUM_WRITE_THREAD; i ++) {
	  ret = pthread_join(write_threads[i], NULL);
	  printf("write pthread %d finished\n", i);

	  if (ret != 0) printf("Error joining write thread %d\n", i);
  }

  syscall(325);
}
