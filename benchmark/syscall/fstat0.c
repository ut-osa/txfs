#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

int main(void) {
  int f1;
  int ret=-1;
  char buf[8];
  struct stat sb;

  syscall(324);

  printf("Current process ID : %d\n", getpid());

  ret = syscall(322);
  if (ret < 0) perror("Error sys_txbegin() ");

  f1 = open("file1", O_CREAT | O_RDWR | O_APPEND, 0644);
  write(f1, "1111111\n", 8);

  if (fstat(f1, &sb) == -1) {
    perror("stat");
  }

  printf("File type:                ");

  switch (sb.st_mode & S_IFMT) {
  case S_IFBLK:  printf("block device\n");            break;
  case S_IFCHR:  printf("character device\n");        break;
  case S_IFDIR:  printf("directory\n");               break;
  case S_IFIFO:  printf("FIFO/pipe\n");               break;
  case S_IFLNK:  printf("symlink\n");                 break;
  case S_IFREG:  printf("regular file\n");            break;
  case S_IFSOCK: printf("socket\n");                  break;
  default:       printf("unknown?\n");                break;
  }

  printf("I-node number:            %ld\n", (long) sb.st_ino);

  printf("Mode:                     %lo (octal)\n",
  (unsigned long) sb.st_mode);

  printf("Link count:               %ld\n", (long) sb.st_nlink);
  printf("Ownership:                UID=%ld   GID=%ld\n",
          (long) sb.st_uid, (long) sb.st_gid);

  printf("Preferred I/O block size: %ld bytes\n",
          (long) sb.st_blksize);
  printf("File size:                %lld bytes\n",
          (long long) sb.st_size);
  printf("Blocks allocated:         %lld\n",
          (long long) sb.st_blocks);

  printf("Last status change:       %s", ctime(&sb.st_ctime));
  printf("Last file access:         %s", ctime(&sb.st_atime));
  printf("Last file modification:   %s", ctime(&sb.st_mtime));

  ret = syscall(323);
  if (ret < 0) perror("Error sys_txend() ");

  close(f1);

  syscall(325);
}
