#include "shared.h"

#define NUM_READ_THREAD 10

static char dir[20];
static long num_cores;

void *start_create(void *ptr) {
  int f0, ret;
  ssize_t ret2;
  char buf[8];
  int *id = (int *) ptr;
  char name[32];
  pid_t pid = syscall(__NR_gettid);

  bind_core((*id) % num_cores);

  printf("Create pthread %d started, current process ID : %d\n", *id, pid);

  sprintf(name, "%s/file%d", dir, *id);

  ret = syscall(322);
  if (ret < 0) perror("\tfs_txbegin");
  
  f0 = open(name, O_CREAT | O_RDWR, 0644);
  if (f0 < 0) perror("\t\topen");
  close(f0);

  ret = syscall(323);
  if (ret < 0) perror("\tfs_txend");

  free(ptr);
}

int main(void) {
  int ret, i;
  char buf[8], cmd[64];
  pthread_t read_threads[NUM_READ_THREAD];

  num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  sprintf(dir, "%s/testdir", PATH_VALUE(TESTDIR));
  sprintf(cmd, "rm -r %s", dir);
  system(cmd);
  mkdir(dir, 0700);

  syscall(324);

  for (i = 0; i < NUM_READ_THREAD; i ++) {
    int *arg = malloc(sizeof(*arg));
    *arg = i;
    ret = pthread_create(&read_threads[i], NULL, start_create, arg);

    if (ret != 0) printf("Error starting thread %d\n", i);
  }

  for (i = 0; i < NUM_READ_THREAD; i ++) {
    ret = pthread_join(read_threads[i], NULL);
    printf("read pthread %d finished\n", i);

    if (ret != 0) printf("Error joining read thread %d\n", i);
  }

  syscall(325);
}
