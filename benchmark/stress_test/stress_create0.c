#include "shared.h"

#define NUM_FILES 250

int main(void) {
  int fd;
  int i, ret=0;
  char name[32], dir[24], cmd[64];
  struct stat st = {0};

  sprintf(dir, "%s/testdir", PATH_VALUE(TESTDIR));
  sprintf(cmd, "rm -r %s", dir);
  system(cmd);
  mkdir(dir, 0700);

  syscall(324);

  printf("Current process ID : %d\n", getpid());

  ret = syscall(322);
  if (ret < 0) printf("Error: sys_txbegin().\n");

  for (i = 0; i < NUM_FILES; i ++) {
    sprintf(name, "%s/file%d", dir, i);
    fd = open(name, O_CREAT | O_RDWR | O_APPEND, 0644);
    if (fd < 0) {
      perror("Open");
    }
    close(fd);
  }

  ret = syscall(323);
  if (ret < 0) perror("sys_txend");

  sync();

  syscall(325);
}
