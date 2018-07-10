#define _GNU_SOURCE
#include <dirent.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#define TX_BEGIN 322
#define TX_COMMIT 323
#define TX_ABORT 326
#define TX_DEBUG_BEGIN 324
#define TX_DEBUG_END 325

#define ECONFLICT 531
#define ENOJOURNAL 532

#define NUM_OPS 15
#define NUM_DIR_OPS 4

#define CREATE_FILE_NO_TX 0
#define DELETE_FILE_NO_TX 1
#define READ_FILE_NO_TX 2
#define WRITE_FILE_NO_TX 3

#define CREATE_FILE_TX 4
#define DELETE_FILE_TX 5
#define READ_FILE_TX 6
#define WRITE_FILE_TX 7

#define EVIL_TX 8
#define OPEN_DIR_TX 9
#define LIST_DIR_TX 10
#define OPEN_NO_CLOSE 11

#define CREATE_DIR_NO_TX 12
#define DELETE_DIR_NO_TX 13
#define CREATE_DIR_TX 14
#define DELETE_DIR_TX 15

#define MAX_RETRIES 10

#define FNAME_LEN 128

#define STR_VALUE(name) #name
#define PATH_VALUE(name) STR_VALUE(name)

#define FILE_DEFINE(num) \
		char file##num[FNAME_LEN]; \
		snprintf(file##num, FNAME_LEN-1, "%s/file%d",	PATH_VALUE(TESTDIR), num);

// (iangneal): According to getdents(2) you have to declare this yourself. WHY?
struct linux_dirent64 {
   ino64_t        d_ino;    /* 64-bit inode number */
   off64_t        d_off;    /* 64-bit offset to next structure */
   unsigned short d_reclen; /* Size of this dirent */
   unsigned char  d_type;   /* File type */
   char           d_name[]; /* Filename (null-terminated) */
};

static inline int getdents64(
    unsigned fd, char *dir_buf, unsigned count) {
  return syscall(SYS_getdents64, fd, dir_buf, count);
}

inline static void bind_core(int core_id) {
  const int NCPUs = sysconf(_SC_NPROCESSORS_CONF);

  core_id = core_id % NCPUs;

  cpu_set_t new_mask;
  CPU_ZERO(&new_mask);
  CPU_SET(core_id, &new_mask);

  int ret = sched_setaffinity(0, sizeof(new_mask), &new_mask);
  assert(0 == ret);

  cpu_set_t old_mask;
  CPU_ZERO(&old_mask);
  ret = sched_getaffinity(0, sizeof(old_mask), &old_mask);
  assert(0 == ret);

  ret = memcmp(&old_mask, &new_mask, sizeof(new_mask));
  assert(0 == ret);
}

void* failure(const char* name, long ret, int thread_num) {
  fprintf(stderr, "Thread %d failed while running function %s with value "
      "%ld.\n", thread_num, name, ret);
  return (void*)ret;
}

// For functions called at random.

static int create_file_tx(const char* dir, int num_files, int thread_num) {
  int f0, ret, retries = 0;
  int file_num = thread_num % num_files;
  char name[FNAME_LEN];

  snprintf(name, FNAME_LEN-1, "%s/file%d", dir, file_num);
  //printf(">>> create_file_tx: %s\n", name);
tx:
  ret = syscall(TX_BEGIN);
  if (ret) {
		perror("\tfs_txbegin");
		return -1;
	}

  f0 = open(name, O_CREAT | O_RDWR, 0644);
  if (f0 < 0 && errno == ECONFLICT) {
    syscall(TX_ABORT);
    goto tx;
	} else if (f0 < 0) {
		perror("\t\topen");
		return -1;
	}
  close(f0);

  ret = syscall(TX_COMMIT);
  if (ret < 0 && errno == ECONFLICT && retries < MAX_RETRIES) {
    retries++;
		goto tx;
	} else if (ret < 0 && retries >= MAX_RETRIES) {
    perror("\tnumber of retries exceeded");
    return -1;
  } else if (ret < 0) {
		perror("\tTX_COMMIT");
		return -1;
	}

	return 0;
}

static int delete_file_tx(const char* dir, int num_files, int thread_num) {
  int ret, retries = 0;
  int file_num = thread_num % num_files;
  char name[FNAME_LEN];

  snprintf(name, FNAME_LEN-1, "%s/file%d", dir, file_num);
  //printf(">>> delete_file_tx: %s\n", name);
tx:
  ret = syscall(TX_BEGIN);
  if (ret) {
		perror("\tfs_txbegin");
		return -1;
	}

  ret = unlink(name);
  if (ret && errno != ENOENT && errno) {
    fprintf(stderr, "errno: %d, ret: %d\n", errno, ret);
    perror("\t\tremove");
    return -1;
  }

  ret = syscall(TX_COMMIT);
  if (ret < 0 && errno == ECONFLICT && retries < MAX_RETRIES) {
    retries++;
		goto tx;
	} else if (ret < 0 && retries >= MAX_RETRIES) {
    perror("\tnumber of retries exceeded");
    return -1;
  } else if (errno == ENOENT) {
    return 0;
  } else if (ret < 0) {
		fprintf(stderr, "\tTX_COMMIT[%d] returned 0x%08x: %s (%d)\n",
        thread_num, ret, strerror(errno), errno);
		return -1;
	}

	return 0;
}

static int read_file_tx(const char* dir, int num_files, int thread_num) {
  int f0, ret, retries = 0;
  int file_num = thread_num % num_files;
  ssize_t ret2;
  char buf[1024];
  char name[FNAME_LEN];

  snprintf(name, FNAME_LEN-1, "%s/file%d", dir, file_num);
tx:
  ret = syscall(TX_BEGIN);
  if (ret) {
		perror("\tfs_txbegin");
		return -1;
	}

  f0 = open(name, O_CREAT | O_RDWR, 0644);
  if (f0 < 0) {
    syscall(TX_ABORT);
    if (errno == ECONFLICT) {
      goto tx;
    } else {
      perror("\t\topen");
      return -1;
    }
	}

  ret2 = read(f0, buf, 1024);
  if (ret2 < 0) {
    syscall(TX_ABORT);
    close(f0);
    if (errno == ECONFLICT) {
      goto tx;
    } else {
      perror("\t\tread");
      return -1;
    }
  }

  ret = close(f0);
  if (ret < 0) {
    syscall(TX_ABORT);
    if (errno == ECONFLICT) {
      goto tx;
    } else {
      perror("\t\tclose");
      return -1;
    }
  }

  ret = syscall(TX_COMMIT);
  if (ret < 0 && errno == ECONFLICT && retries < MAX_RETRIES) {
    retries++;
		goto tx;
	} else if (ret < 0 && retries >= MAX_RETRIES) {
    perror("\tnumber of retries exceeded");
    return -1;
  } else if (ret < 0) {
		perror("\tTX_COMMIT");
		return -1;
	}

	return 0;
}

static int write_file_tx(const char* dir, int num_files, int thread_num) {
  int f0, ret, retries = 0;
  int file_num = thread_num % num_files;
  ssize_t ret2;
  char buf[1024];
  char name[FNAME_LEN];

  snprintf(name, FNAME_LEN, "%s/file%d", dir, file_num);

  f0 = open("/dev/urandom", O_RDONLY);
  if (f0 < 0) {
    fprintf(stderr, "Could not open /dev/urandom: %s.\n", strerror(errno));
    return -1;
  }
  ret2 = read(f0, buf, 1024);
  if (ret2 != 1024) {
    fprintf(stderr, "Could not read requested buffer size: %s.\n",
        strerror(errno));
    return -1;
  }
  close(f0);

tx:
  ret = syscall(TX_BEGIN);
  if (ret) {
		perror("\tfs_txbegin");
		return -1;
	}

  f0 = open(name, O_CREAT | O_RDWR, 0644);
  if (f0 < 0) {
    syscall(TX_ABORT);
    if (errno == ECONFLICT) {
      goto tx;
    } else {
      perror("\t\topen");
      return -1;
    }
	}

  ret2 = write(f0, buf, 1024);
  if (ret2 < 1024) {
    close(f0);
    syscall(TX_ABORT);
    if (errno == ECONFLICT) {
      goto tx;
    } else {
      perror("\t\twrite");
      return -1;
    }
  }

  ret = close(f0);
  if (ret) {
    syscall(TX_ABORT);
    if (errno == ECONFLICT) {
      goto tx;
    } else {
      perror("close");
      return -1;
    }
  }

  ret = syscall(TX_COMMIT);
  if (ret < 0 && errno == ECONFLICT && retries < MAX_RETRIES) {
    retries++;
		goto tx;
	} else if (ret < 0 && retries >= MAX_RETRIES) {
    perror("\tnumber of retries exceeded");
    return -1;
  } else if (ret < 0) {
		perror("\tTX_COMMIT");
		return -1;
	}

	return 0;
}

static int create_dir_tx(const char* dir, int num_dirs, int thread_num) {
  int ret, retries = 0;
  int dir_num = thread_num % num_dirs;
  char name[FNAME_LEN];

  snprintf(name, FNAME_LEN-1, "%s/dir%d", dir, dir_num);
tx:
  ret = syscall(TX_BEGIN);
  if (ret) {
		perror("\tfs_txbegin");
		return -1;
	}

  ret = mkdir(name, 0777);
  if (ret) {
    ret = syscall(TX_ABORT);
    if (ret) {
      fprintf(stderr, "Could not abort transaction (errno: %d).\n", errno);
      return -1;
    }

    if (ret) {
      perror("\t\tmkdir");
      return -1;
    }
  }

  ret = syscall(TX_COMMIT);
  if (ret < 0 && errno == ECONFLICT && retries < MAX_RETRIES) {
    retries++;
		goto tx;
  } else if (errno == EEXIST) {
    return 0;
	} else if (ret < 0 && retries >= MAX_RETRIES) {
    perror("\tnumber of retries exceeded");
    return -1;
  } else if (ret < 0) {
		perror("\tTX_COMMIT");
		return -1;
	}

	return 0;
}

static int delete_dir_tx(const char* dir, int num_dirs, int thread_num) {
  int ret, retries = 0;
  int dir_num = thread_num % num_dirs;
  char name[FNAME_LEN];
  struct stat st = {0};

  snprintf(name, FNAME_LEN-1, "%s/dir%d", dir, dir_num);
tx:
  ret = syscall(TX_BEGIN);
  if (ret) {
		perror("\tTX_BEGIN");
		return -1;
	}

  if (stat(name, &st) == -1) {
    ret = syscall(TX_ABORT);
    if (ret) {
      fprintf(stderr, "Could not abort transaction (errno: %d).\n", errno);
      return -1;
    }
    return 0;
  }

  ret = remove(name);
  if (ret < 0) {
		perror("\t\tremove(dir)");
		return -1;
	}

  ret = syscall(TX_COMMIT);
  if (ret < 0 && errno == ECONFLICT && retries < MAX_RETRIES) {
    syscall(TX_ABORT);
    retries++;
		goto tx;
	} else if (ret < 0 && retries >= MAX_RETRIES) {
    perror("\tnumber of retries exceeded");
    return -1;
  } else if (errno == ENOENT) {
    return 0;
  } else if (ret < 0) {
		fprintf(stderr, "\tTX_COMMIT[%d] returned 0x%08x: %s (%d)\n",
        thread_num, ret, strerror(errno), errno);
		return -1;
	}
  //printf("<<< delete_dir_tx\n");
	return 0;
}

static int create_file_no_tx(const char* dir, int num_files, int thread_num) {
  int f0, file_num = thread_num % num_files;
  char name[FNAME_LEN];

  snprintf(name, FNAME_LEN-1, "%s/file%d", dir, file_num);

  f0 = open(name, O_CREAT | O_RDWR, 0644);
  if (f0 < 0) {
		fprintf(stderr, "\t\topen: %s\n", strerror(errno));
		return -1;
	}
  close(f0);

	return 0;
}

static int delete_file_no_tx(const char* dir, int num_files, int thread_num) {
  int f0, ret;
  int file_num = thread_num % num_files;
  char name[FNAME_LEN];

  snprintf(name, FNAME_LEN-1, "%s/file%d", dir, file_num);

  f0 = open(name, O_CREAT | O_RDWR, 0644);
  if (f0 < 0) {
		fprintf(stderr, "\t\topen: %s\n", strerror(errno));
		return -1;
	}
  close(f0);

  ret = unlink(name);
  if (ret) {
    ret = errno;
		fprintf(stderr, "\t\tremove: %s\n", strerror(errno));
    return ret;
  }

	return 0;
}

static int read_file_no_tx(const char* dir, int num_files, int thread_num) {
  int f0, file_num = thread_num % num_files;
  ssize_t ret2;
  char buf[1024];
  char name[FNAME_LEN];

  snprintf(name, FNAME_LEN-1, "%s/file%d", dir, file_num);

  f0 = open(name, O_CREAT | O_RDWR, 0644);
  if (f0 < 0) {
		fprintf(stderr, "\t\topen: %s\n", strerror(errno));
		return -1;
	}

  ret2 = read(f0, buf, 1024);
  if (ret2 < 0) {
    ret2 = errno;
		fprintf(stderr, "\t\tread: %s\n", strerror(errno));
    close(f0);
    return ret2;
  }

  close(f0);

	return 0;
}

static int write_file_no_tx(const char* dir, int num_files, int thread_num) {
  int f0, file_num = thread_num % num_files;
  ssize_t ret2;
  char buf[1024];
  char name[64];

  snprintf(name, 63, "%s/file%d", dir, file_num);

  f0 = open("/dev/urandom", O_RDONLY);
  read(f0, buf, 1024);
  close(f0);

  f0 = open(name, O_CREAT | O_RDWR, 0644);
  if (f0 < 0) {
		fprintf(stderr, "\t\topen: %s\n", strerror(errno));
		return -1;
	}

  ret2 = write(f0, buf, 1024);
  if (ret2 < 1024) {
    ret2 = errno;
		fprintf(stderr, "\t\twrite: %s\n", strerror(errno));
    close(f0);
    return ret2;
  }

  close(f0);

	return 0;
}

static int create_dir_no_tx(const char* dir, int num_dirs, int thread_num) {
  int ret;
  int dir_num = thread_num % num_dirs;
  char name[FNAME_LEN];
  struct stat st;

  snprintf(name, FNAME_LEN-1, "%s/dir%d", dir, dir_num);

  if (stat(name, &st)) {
    rmdir(name);
  }

  ret = mkdir(name, 0777);
  if (ret < 0 && errno != EEXIST) {
		perror("\t\tmkdir");
		return -1;
	}

	return 0;
}

static int delete_dir_no_tx(const char* dir, int num_dirs, int thread_num) {
  int ret;
  int dir_num = thread_num % num_dirs;
  char name[FNAME_LEN];
  struct stat st = {0};

  snprintf(name, FNAME_LEN-1, "%s/dir%d", dir, dir_num);

  if (stat(name, &st) == -1) {
    mkdir(name, 0777);
  }

  ret = rmdir(name);
  if (ret < 0 && errno != ENOENT) {
		perror("\t\tremove(dir)");
		return -1;
	}

	return 0;
}

static int evil_tx(const char* dir, int num_files, int thread_num) {
  pid_t pid, cpid;
  int ret;

  pid = fork();
  if (!pid) {
    // Child: start transaction then die.
    if (syscall(TX_BEGIN)) {
      fprintf(stderr, "Error in tx_begin.\n");
      exit(EXIT_FAILURE);
    }
    exit(0);
  } else {
    do {
      cpid = wait(&ret);
    } while (cpid != pid && cpid != -1);
    if (ret && errno != ECHILD) {
      fprintf(stderr, "Error in child process: %d\n", ret);
      return ret;
    }
    return 0;
  }
  return -1;
}

static int open_dir_tx(const char* dir) {
  int ret, retries = 0;
  DIR *directory;
  char name[FNAME_LEN];

  snprintf(name, FNAME_LEN-1, "%s/dir_open", dir);
  system("echo 3 > /proc/sys/vm/drop_caches");
tx:
  ret = syscall(TX_BEGIN);
  if (ret) {
    perror("\tfs_txbegin");
    return -1;
  }

  directory = opendir(name);
  if (directory <= 0) {
    syscall(TX_ABORT);
    if (errno == ECONFLICT) {
      goto tx;
    } else {
      perror("\t\topen");
      return -1;
    }
  }
  closedir(directory);

  ret = syscall(TX_COMMIT);
  if (ret < 0 && errno == ECONFLICT && retries < MAX_RETRIES) {
    retries++;
    goto tx;
  } else if (errno == EEXIST) {
    return 0;
  } else if (ret < 0 && retries >= MAX_RETRIES) {
    perror("\tnumber of retries exceeded");
    return -1;
  } else if (ret < 0) {
    perror("\tTX_COMMIT");
    return -1;
  }

  return 0;
}

#define DENT_BUF_SIZE 1024
//#define PRINT_DIR_CONTENT

static int list_dir_tx(const char* dir) {
  int fd = 0, ret = 0, retries = 0, num = 10, bpos;
  char dent_buf[DENT_BUF_SIZE];
  struct linux_dirent64 *d;

  system("echo 3 > /proc/sys/vm/drop_caches");
tx:
  ret = syscall(TX_BEGIN);
  if (ret) {
    perror("\tfs_txbegin");
    return -1;
  }

  fd = open(dir, O_RDONLY | O_DIRECTORY);
  if (fd < 0) {
    syscall(TX_ABORT);
    if (errno == ECONFLICT) {
      goto tx;
    } else {
      perror("\t\topen");
      return -1;
    }
  }

  // Just get one entry from the directory.
  ret = getdents64(fd, dent_buf, num * sizeof(struct linux_dirent64));
  if (ret < 0) {
    syscall(TX_ABORT);
    if (errno == ECONFLICT) {
      goto tx;
    } else {
      fprintf(stderr, "getdents64: %d (%s)\n", errno, strerror(errno));
      return -1;
    }
  }
#ifdef PRINT_DIR_CONTENT
  printf("--------------- nread=%d ---------------\n", ret);
  printf("inode#    file type  d_reclen  d_off   d_name\n");
  for (int bpos = 0; bpos < ret;) {
    d = (struct linux_dirent64 *) (dent_buf + bpos);
    printf("%8ld  %-10s %4d %10lld  %s\n", d->d_ino,
		    (d->d_type == DT_REG) ?  "regular" :
		    (d->d_type == DT_DIR) ?  "directory" :
		    (d->d_type == DT_FIFO) ? "FIFO" :
		    (d->d_type == DT_SOCK) ? "socket" :
		    (d->d_type == DT_LNK) ?  "symlink" :
		    (d->d_type == DT_BLK) ?  "block dev" :
		    (d->d_type == DT_CHR) ?  "char dev" : "???",
		    d->d_reclen, (long long) d->d_off, d->d_name);
    bpos += d->d_reclen;
  }
#endif

  ret = syscall(TX_COMMIT);
  if (ret < 0 && errno == ECONFLICT && retries < MAX_RETRIES) {
    retries++;
    goto tx;
  } else if (ret < 0 && retries >= MAX_RETRIES) {
    perror("\tnumber of retries exceeded");
    return -1;
  } else if (ret < 0) {
    perror("\tTX_COMMIT");
    return -1;
  }

  close(fd);

  return 0;
}

static int open_no_close(const char* dir) {
  int ret, retries = 0, f0;
  char name[FNAME_LEN];

  snprintf(name, FNAME_LEN-1, "%s/file_open", dir);
  /** Flush to force a lookup_real_memlog. */
  system("echo 3 > /proc/sys/vm/drop_caches");
tx:
  ret = syscall(TX_BEGIN);
  if (ret) {
    perror("\tfs_txbegin");
    return -1;
  }

  f0 = open(name, O_CREAT | O_RDWR, 0644);
  if (f0 < 0) {
    syscall(TX_ABORT);
    if (errno == ECONFLICT) {
      goto tx;
    } else {
      perror("\t\topen");
      return -1;
    }
  }

  ret = syscall(TX_COMMIT);
  if (ret < 0 && errno == ECONFLICT && retries < MAX_RETRIES) {
    retries++;
    goto tx;
  } else if (errno == EEXIST) {
    return 0;
  } else if (ret < 0 && retries >= MAX_RETRIES) {
    perror("\tnumber of retries exceeded");
    return -1;
  } else if (ret < 0) {
    perror("\tTX_COMMIT");
    return -1;
  }
  return 0;
}
