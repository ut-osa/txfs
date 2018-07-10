#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(void) {
  int f0, f1, f2, f3;
  int ret=-1;
  char buf[8];

  syscall(324);

  printf("Current process ID : %d\n", getpid());

  f1 = open("file1", O_CREAT | O_RDWR | O_APPEND, 0644);
  f2 = open("file2", O_CREAT | O_WRONLY | O_APPEND, 0644);

  write(f1, "1111111\n", 8);

  ret = syscall(322);
  if (ret < 0) printf("Error: sys_txbegin().\n");

  write(f1, "1111111\n", 8);
  write(f2, "2222222\n", 8);

  ret = syscall(326);
  if (ret < 0) printf("Error: sys_txend().\n");

  write(f2, "2222222\n", 8);

  close(f1);
  close(f2);

  syscall(325);
}
