#include "shared.h"

int main(void) {
  int f0, f1, f2, f3;
  int ret=-1;
  char buf[8];
  DEFINE_FILES;

  syscall(324);

  printf("Current process ID : %d\n", getpid());

  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  f1 = open(file1, O_CREAT | O_RDWR | O_APPEND, 0644);
  f2 = open(file2, O_CREAT | O_WRONLY | O_APPEND, 0644);
  f3 = open(file3, O_CREAT | O_WRONLY | O_APPEND, 0644);

  write(f1, "1111111\n", 8);
  write(f3, "3333333\n", 8);

  ret = syscall(322);
  if (ret < 0) perror("Error sys_txbegin(): ");

  write(f0, "0000000\n", 8);
  write(f1, "1111111\n", 8);
  write(f2, "2222222\n", 8);

  ret = syscall(326);
  if (ret < 0) perror("Error sys_txend(): ");

  write(f2, "2222222\n", 8);
  write(f3, "3333333\n", 8);

  close(f0);
  close(f1);
  close(f2);
  close(f3);

  syscall(325);
}
