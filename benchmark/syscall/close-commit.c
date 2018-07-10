#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

int main(void) {
  fprintf(stderr, "[pid %d]\n", getpid());

  unlink("close.t");
  int fd = open("close.t", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    perror("Error making test file\n");
    return -1;
  }
  close(fd);

  syscall(324);

  if (syscall(322) < 0) {
    fprintf(stderr, "Error starting fs tx\n");
    return -1;
  }
  fd = open("close.t", O_RDWR);
  if (fd < 0) {
    perror("Error opening file");
    return -1;
  }
  int res = write(fd, "abcde", 5);
  if (res < 0) {
    perror("Error writing to file");
    close(fd);
    syscall(326);
    return -1;
  }
  close(fd);
  // Crashes on tx commit.
  if (syscall(323) < 0) {
    fprintf(stderr, "Error ending fs tx\n");
    return -1;
  }

  syscall(325);
  return 0;
}
