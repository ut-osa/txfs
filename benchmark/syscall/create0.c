#include "shared.h"

int main(void) {
  int f0, f1, f2, f3;
  int ret=-1;
  char buf[8];
  DEFINE_FILES;

  syscall(324);

  unlink(file0);
  unlink(file1);
  unlink(file2);
  unlink(file3);

  printf("Current process ID : %d\n", getpid());

  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);

  ret = syscall(322);
  if (ret < 0) perror("Error sys_txbegin(): ");

  f1 = open(file1, O_CREAT | O_RDWR | O_APPEND, 0644);

  f2 = open(file2, O_CREAT | O_WRONLY | O_APPEND, 0644);

  ret = syscall(323);
  if (ret < 0) perror("Error sys_txend(): ");

  f3 = open(file3, O_CREAT | O_WRONLY | O_APPEND, 0644);

  close(f0);
  close(f1);
  close(f2);
  close(f3);

  syscall(325);
}
