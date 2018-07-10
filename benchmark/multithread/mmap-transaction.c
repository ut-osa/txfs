/**
 * Attempt to create and commit a file transaction through a mapped region of
 * memory -- may or may not actually be supported by the file system.
 */
#include "shared.h"

#include <sys/mman.h>

#define FILE_NAME "tmp_mmap.bin"

#define STR(x) #x

void print_syscall_err(const char* name, int rc) {
	fprintf(stderr, "syscall(%s) returned rc = %d (errno = %d)\n",
			name, rc, errno);
}

#if 0
static void* routine(void* arg) {
  FILE_DEFINE((int)arg);
}
#endif

int main(int argc, const char** argv) {
	int fd, rc, size = 1024;
	const char data[] = "This is the data string.";
	char file_name[128];
	char* mem;

	snprintf(file_name, 127, "%s/%s", PATH_VALUE(TESTDIR), FILE_NAME);

	fd = open(file_name, O_RDWR | O_CREAT, 0777);
	if (fd < 0) {
		fprintf(stderr, "Could not open file named \'%s\': %s.\n", file_name,
        strerror(errno));
		return -1;
	}

  syscall(TX_DEBUG_BEGIN);
	// Stretch our new file to the appropriate size, but clear it first.
  rc = ftruncate(fd, 0);
  if (rc) {
    perror("ftruncate");
    goto err;
  }

  rc = ftruncate(fd, size);
  if (rc) {
    perror("ftruncate");
    goto err;
  }

  // Make sure our changes have propagated.
  rc = fdatasync(fd);
  if (rc) {
    perror("fdatasync");
    goto err;
  }

	// Map some memory as file-backed.
	mem = (char*) mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (!mem) {
		perror("Could not mmap");
    goto err;
	}

	// Write some data to the memory as a transaction.
	rc = syscall(TX_BEGIN);
	if (rc) {
		print_syscall_err(STR(TX_BEGIN), rc);
    goto err;
	}

	rc = snprintf(mem, size, data);
  if (rc != strlen(data)) {
    perror("snprintf");
    goto err;
  }

  rc = fdatasync(fd);
  if (rc) {
    perror("fdatasync");
    goto err;
  }
	sync();
	rc = syscall(TX_COMMIT);
	if (rc) {
		print_syscall_err(STR(TX_COMMIT), rc);
    goto err;
	}

	// Now we try to read the data and see if it stuck.
	if (strcmp(mem, data)) {
		fprintf(stderr, "Error writing to memory: \'%s\' should be \'%s\'\n",
				mem, data);
    goto err;
	} else {
		fprintf(stdout, "Memory successfully written.\n>>>%s\n", mem);
	}

	// End testing.
	rc = munmap(mem, size);
	if (rc) {
		perror("Could not munmap");
    goto err;
	}

	close(fd);
	syscall(TX_DEBUG_END);
  return 0;

err:
  close(fd);
  syscall(TX_DEBUG_END);
  return -1;
}
