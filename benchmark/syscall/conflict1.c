#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define BUF_SIZE 8

int main(void) {
  int f0, f1, f2, f3;
  int ret=-1;
  char buf[BUF_SIZE];

  syscall(324);

  printf("Current process ID : %d\n", getpid());

  f0 = open("file0", O_CREAT | O_RDWR | O_APPEND, 0644);
  f1 = open("file1", O_CREAT | O_RDWR | O_APPEND, 0644);
  f2 = open("file2", O_CREAT | O_WRONLY | O_APPEND, 0644);
  f3 = open("file3", O_CREAT | O_WRONLY | O_APPEND, 0644);

  write(f1, "1111111\n", 8);
  write(f2, "2222222\n", 8);

  ret = syscall(322);
  if (ret < 0) printf("Error: sys_txbegin().\n");
  ret = read(f1, buf, BUF_SIZE);
  ret = syscall(323);
  if (ret < 0) printf("TX1 Error: sys_txend() %d\n", errno);  /* expect 531 */

  write(f3, "3333333\n", 8);

  sync();

  ret = read(f1, buf, BUF_SIZE);

  ret = syscall(322);
  if (ret < 0) printf("Error: sys_txbegin().\n");
  write(f1, "1111111\n", 8);
  ret = read(f1, buf, BUF_SIZE);
  ret = syscall(323);
  if (ret < 0) printf("TX2 Error: sys_txend() %d\n", errno);

  ret = syscall(322);
  if (ret < 0) printf("Error: sys_txbegin().\n");
  ret = read(f1, buf, BUF_SIZE);
  ret = syscall(323);
  if (ret < 0) printf("TX3 Error: sys_txend() %d\n", errno);  /* expect 531 */

  ret = syscall(322);
  if (ret < 0) printf("Error: sys_txbegin().\n");
  ret = read(f1, buf, BUF_SIZE);
  write(f1, "1111111\n", 8);
  ret = syscall(323);
  if (ret < 0) printf("TX4 Error: sys_txend() %d\n", errno);  /* expect 531 */

  sync();

  ret = read(f1, buf, BUF_SIZE);

  ret = syscall(322);
  if (ret < 0) printf("Error: sys_txbegin().\n");
  ret = read(f1, buf, BUF_SIZE);
  write(f1, "1111111\n", 8);
  ret = syscall(323);
  if (ret < 0) printf("TX2 Error: sys_txend() %d\n", errno);

  close(f0);
  close(f1);
  close(f2);
  close(f3);

  syscall(325);
}
