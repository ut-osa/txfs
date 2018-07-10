#include "shared.h"

int main(void) {
  int f0, f1, f2, f3;
  int ret=-1;
  ssize_t size;
  char buf[9];
  DEFINE_FILES;

  buf[8] = 0;

  syscall(324);

  printf("Current process ID : %d\n", getpid());

  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  if (f0 < 0) perror("Open 0");
  write(f0, "0000000\n", 8);

  ret = syscall(322);
  if (ret < 0) perror("Error sys_txbegin(): ");

  f1 = open(file1, O_CREAT | O_RDWR | O_APPEND, 0644);
  if (f1 < 0) perror("Open 1");
  write(f1, "1111111\n", 8);

  f2 = open(file2, O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (f2 < 0) perror("Open 2");
  write(f2, "2222222\n", 8);

  ret = syscall(323);
  if (ret < 0) perror("Error sys_txend(): ");

  f3 = open(file3, O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (f3 < 0) perror("Open 3");
  write(f3, "3333333\n", 8);

  close(f0);
  close(f1);
  close(f2);
  close(f3);

  sync();
  system("echo 3 > /proc/sys/vm/drop_caches");

  // Confirm the files contain what they should.
  f0 = open(file0, O_RDONLY);
  if (f0 < 0) perror("Open 0");
  f1 = open(file1, O_RDONLY);
  if (f1 < 0) perror("Open 1");
  f2 = open(file2, O_RDONLY);
  if (f2 < 0) perror("Open 2");
  f3 = open(file3, O_RDONLY);
  if (f3 < 0) perror("Open 3");

  size = read(f0, buf, 8);
  if (size != 8) perror("Read");
  if (strcmp(buf, "0000000\n"))
    fprintf(stderr, "Invalid contents: %s\n", buf);
  size = read(f1, buf, 8);
  if (size != 8) perror("Read");
  if (strcmp(buf, "1111111\n"))
    fprintf(stderr, "Invalid contents: %s\n", buf);
  size = read(f2, buf, 8);
  if (size != 8) perror("Read");
  if (strcmp(buf, "2222222\n"))
    fprintf(stderr, "Invalid contents: %s\n", buf);
  size = read(f3, buf, 8);
  if (size != 8) perror("Read");
  if (strcmp(buf, "3333333\n"))
    fprintf(stderr, "Invalid contents: %s\n", buf);

  syscall(325);
}
