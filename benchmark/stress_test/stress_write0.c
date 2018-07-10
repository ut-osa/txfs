#include "shared.h"

#define NUM_FILES 250

int main(void) {
  int fd;
  int i;
  long int ret = 0;
  char name[32], dir[24];
  struct stat st = {0};

  //system("rm -r testdir");
  //mkdir("testdir", 0700);
  sprintf(dir, "%s/testdir", PATH_VALUE(TESTDIR));

  syscall(324);

  printf("Current process ID : %d\n", getpid());

  ret = syscall(322);
  if (ret < 0) printf("Error: sys_txbegin().\n");

  for (i = 0; i < NUM_FILES; i ++) {
    sprintf(name, "%s/file%d", dir, i);
    fd = open(name, O_RDWR | O_APPEND, 0644);
    if (fd < 0) {
      perror("Open");
    }
    write(fd, "0000000\n", 8);
    close(fd);
  }

  ret = syscall(323);
  if (ret < 0) perror("sys_txend");

  sync();

  syscall(325);
}
