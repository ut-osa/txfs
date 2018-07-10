#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#define FILE_NAME "read.t"
#define BUF_SIZE 4096

int main(void) {
  // Housekeeping, get a buffer of random data and remove / recreate test files.
  fprintf(stderr, "[pid %d]\n", getpid());

  if (access(FILE_NAME, F_OK) == -1) {
    fprintf(stderr, "Test file %s doesn't exist\n", FILE_NAME);
    return -1;
  }

  // Begin testing things.
  syscall(324);

  if (syscall(322) < 0) {
    fprintf(stderr, "Error starting fs tx\n");
    return -1;
  }
  int fd = open(FILE_NAME, O_RDONLY);
  if (fd < 0) {
    perror("Error opening test file");
  }

  int count = 0;
  int res = -1;
  char buf[BUF_SIZE];
  while (count < BUF_SIZE) {
    res = read(fd, buf + count, BUF_SIZE - count);
    if (res < 0) {
      perror("Error reading from file");
      close(fd);
      syscall(326);
      return -1;
    }
    count += res;
  }

  close(fd);
  if (syscall(323) < 0) {
    fprintf(stderr, "Error ending fs tx\n");
    return -1;
  }
  
  syscall(325);

  return 0;
}

