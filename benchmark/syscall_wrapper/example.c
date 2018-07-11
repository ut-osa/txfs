#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "syscall_wrapper.h"

#define STR_VALUE(name) #name
#define PATH_VALUE(name) STR_VALUE(name)

#define FILE_DEFINE(num)			\
		char file##num[20];		\
		sprintf(file##num, "%s/file%d",	PATH_VALUE(TESTDIR), num);

#define DEFINE_FILES				\
	FILE_DEFINE(0);				\
	FILE_DEFINE(1);				\
	FILE_DEFINE(2);				\
	FILE_DEFINE(3);

int main(void) {
  int f0, f1, f2, f3;
  int ret=-1;
  ssize_t size;
  char buf[9];
  DEFINE_FILES;

  buf[8] = 0;

  fs_tx_dbg_begin();

  printf("Current process ID : %d\n", getpid());

  ret = fs_tx_begin();
  if (ret < 0) perror("Error fs_tx_begin() ");

  f0 = open(file0, O_CREAT | O_RDWR | O_APPEND, 0644);
  if (f0 < 0) perror("Open 0");
  write(f0, "0000000\n", 8);

  f1 = open(file1, O_CREAT | O_RDWR | O_APPEND, 0644);
  if (f1 < 0) perror("Open 1");
  write(f1, "1111111\n", 8);

  f2 = open(file2, O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (f2 < 0) perror("Open 2");
  write(f2, "2222222\n", 8);

  f3 = open(file3, O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (f3 < 0) perror("Open 3");
  write(f3, "3333333\n", 8);

  ret = fs_tx_end(TX_DURABLE);
  if (ret < 0) perror("Error fs_tx_end() ");

  close(f0);
  close(f1);
  close(f2);
  close(f3);

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

  fs_tx_dbg_end();
}
