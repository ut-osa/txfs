#include "shared.h"

int main(void) {
  int f0;
  int ret=-1;
  char buf[8];
  DEFINE_1_FILE;

  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  write(f0, "1111111\n", 8);
  close(f0);

  syscall(324);

  printf("Current process ID : %d\n", getpid());

  ret = syscall(322);
  if (ret < 0) perror("Error sys_txbegin() ");

  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  write(f0, "1111111\n", 8);
  close(f0);

  unlink(file0);

  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  write(f0, "2222222\n", 8);
  close(f0);

#if 0
  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  close(f0);
#endif

  unlink(file0);

#if 0
  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  write(f0, "2222222\n", 8);
  close(f0);

  unlink(file0);

  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  write(f0, "2222222\n", 8);
  close(f0);

  unlink(file0);
#endif

  ret = syscall(323);
  //ret = syscall(326);
  if (ret < 0) perror("Error sys_txend() ");

  //f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  write(f0, "3333333\n", 8);
  perror("As expected: Write");
  close(f0);

  syscall(325);
}
