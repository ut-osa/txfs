#include "shared.h"
#include <time.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096
#define NUM_READ_THREAD 1
#define NUM_WRITE_THREAD 1
#define FILE_SIZE 64
#define READ_CHAR 10
#define LOOP_READ 20
#define LOOP_WRITE 5

struct input_args {
  int id;
  int transactional;
};

static char file0[32];
static long num_cores;

/* Note: mmap() currently unsupported.
 * This test checks whether transactional or non-transactional reads on a
 * buffer mapped by mmap() can read pages from a transactional write of another
 * thread performed by mmap(). Expected behavior: non-transactional reads
 * get the global data, while transactional reads return -ECONFLICT. */

/* Transactional reads. */
void *start_read(void *ptr) {
  int f0, ret, i, j, pos;
  struct input_args *input = (struct input_args*) ptr;
  ssize_t ret2;
  char *buf_mapped, *buf = (char *) malloc(READ_CHAR);
  pid_t pid = syscall(__NR_gettid);

  bind_core(input->id % num_cores);

  printf("read pthread started, current process ID : %d\n", pid);

  f0 = open(file0, O_RDWR, 0644);
  if (f0 < 0) printf("\tError: open() ret = %d\n", f0);

  buf_mapped = (char *) mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE,
		  MAP_SHARED, f0, 0);
  if (buf_mapped == MAP_FAILED) {
    perror("mmap");
    goto exit_err;
  }

  for (j = 0; j < LOOP_READ; j++) {
    if (input->transactional) {
      ret = syscall(322);
      if (ret < 0) perror("\tread fs_txbegin");
    }

    for (i = 0; i < READ_CHAR; i ++) {
      pos = rand() % FILE_SIZE;
      buf[i] = buf_mapped[pos];
      if (ret2 < 0) perror("\tread");
    }

    if (ret2 >= 0) printf("Thread %d read: %s\n", input->id, buf);

    if (input->transactional) {
      ret = syscall(323);
      if (ret < 0) perror("\tread fs_txend");
    }
    usleep(1000000);
  }

  munmap(buf_mapped, FILE_SIZE);
exit_err:
  close(f0);
  free(buf);
  free(ptr);
}

/* Transactional writes. */
void *start_write(void *ptr) {
  int f0, i, j, ret;
  int *id = (int *) ptr;
  ssize_t ret2;
  char buf, *buf_mapped;
  pid_t pid = syscall(__NR_gettid);

  bind_core((*id + NUM_READ_THREAD) % num_cores);

  printf("write pthread started, current process ID : %d\n", pid);

  f0 = open(file0, O_RDWR, 0644);
  if (f0 < 0) perror("\topen");

  buf_mapped = (char *) mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE,
		  MAP_SHARED, f0, 0);
  if (buf_mapped == MAP_FAILED) {
    perror("mmap");
    goto exit_err;
  }

  for (j = 0; j < LOOP_WRITE; j++) {
    buf = (char) ((*id) % (90 - 65 + 1) + 65 + j);
    ret = syscall(322);
    printf("Enter transactional write.\n");
    if (ret < 0) perror("\twrite fs_txbegin");

    for (i = 0; i < FILE_SIZE; i++) {
      buf_mapped[i] = buf;
    }
    getchar();
    printf("Committing write.\n");
    ret = syscall(323);
    if (ret < 0) perror("\twrite fs_txend");
    getchar();
  }

  fsync(f0);

  munmap(buf_mapped, FILE_SIZE);
exit_err:
  close(f0);
  free(ptr);
}

int main(int argc, char* const argv[]) {
  int ret, i, c, fd;
  char cmd[80];
  int is_transactional = 0;
  pthread_t read_threads[NUM_READ_THREAD];
  pthread_t write_threads[NUM_WRITE_THREAD];

  num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  while ( (c = getopt(argc, argv, "th")) != -1 ){
    switch(c) {
      case 't':
        is_transactional = 1;
        break;
      case 'h':
        printf("Usage: %s [-t (Use transactional reads)] \n"
               "[-h prints this message] \n",
	       argv[0]);
        return 0;
      case '?':
        fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
        return -1;
    }
  }

  sprintf(file0, "%s/file0", PATH_VALUE(TESTDIR));
  sprintf(cmd, "rm -f %s", file0);
  system(cmd);
  sprintf(cmd, "touch %s", file0);
  system(cmd);
  sprintf(cmd, "dd if=/dev/zero bs=%d count=1 | tr \"\\000\" \"\\060\"  >%s", FILE_SIZE, file0);
  system(cmd);
  srand(time(NULL));

  syscall(324);

  for (i = 0; i < NUM_READ_THREAD; i ++) {
    struct input_args *arg = malloc(sizeof(*arg));
    arg->id;
    arg->transactional = is_transactional;
    ret = pthread_create(&read_threads[i], NULL, start_read, arg);

    if (ret != 0) printf("Error starting thread %d\n", i);
  }

  for (i = 0; i < NUM_WRITE_THREAD; i ++) {
    int *arg = malloc(sizeof(*arg));
    *arg = i;
    ret = pthread_create(&write_threads[i], NULL, start_write, arg);

    if (ret != 0) printf("Error starting thread %d\n", i);
  }

  for (i = 0; i < NUM_READ_THREAD; i ++) {
    ret = pthread_join(read_threads[i], NULL);
    printf("read pthread %d finished\n", i);

    if (ret != 0) printf("Error joining read thread %d\n", i);
  }

  for (i = 0; i < NUM_WRITE_THREAD; i ++) {
	  ret = pthread_join(write_threads[i], NULL);
	  printf("write pthread %d finished\n", i);

	  if (ret != 0) printf("Error joining write thread %d\n", i);
  }

  syscall(325);
}
