#include "shared.h"

#include <time.h>

static char dir[128];
static long num_cores;
static int num_files;
static int num_ops = NUM_OPS;

// If num_tasks = -1, then loop forever until there's an error.
typedef struct {
  int thread_num, num_tasks;
} arg_t;

void* start_tasks(void* ptr) {
  arg_t* args = (arg_t*)ptr;
  int thread_num = args->thread_num;
  int num_tasks = args->num_tasks;
  int i = 0;
  long ret;
  pid_t pid = syscall(__NR_gettid);

  bind_core(thread_num % num_cores);

  printf("Create pthread %d started, current process ID : %d\n",
      thread_num, pid);

  while (i++ < num_tasks || num_tasks == -1) {
    switch(rand() % num_ops) {
      case CREATE_FILE_TX:
        ret = create_file_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(CREATE_FILE_TX), ret, thread_num);
        }
        break;
      case DELETE_FILE_TX:
        ret = delete_file_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(DELETE_FILE_TX), ret, thread_num);
        }
        break;
      case READ_FILE_TX:
        ret = read_file_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(READ_FILE_TX), ret, thread_num);
        }
        break;
      case WRITE_FILE_TX:
        ret = write_file_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(WRITE_FILE_TX), ret, thread_num);
        }
        break;
      case CREATE_FILE_NO_TX:
        ret = create_file_no_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(CREATE_FILE_NO_TX), ret, thread_num);
        }
        break;
      case DELETE_FILE_NO_TX:
        ret = delete_file_no_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(DELETE_FILE_NO_TX), ret, thread_num);
        }
        break;
      case READ_FILE_NO_TX:
        ret = read_file_no_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(READ_FILE_NO_TX), ret, thread_num);
        }
        break;
      case WRITE_FILE_NO_TX:
        ret = write_file_no_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(WRITE_FILE_NO_TX), ret, thread_num);
        }
        break;
      case EVIL_TX:
        ret = evil_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(EVIL_TX), ret, thread_num);
        }
        break;
      case OPEN_DIR_TX:
        ret = open_dir_tx(dir);
        if (ret) {
          return failure(STR_VALUE(OPEN_DIR_TX), ret, thread_num);
        }
        break;
      case CREATE_DIR_TX:
        ret = create_dir_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(CREATE_DIR_TX), ret, thread_num);
        }
        break;
      case DELETE_DIR_TX:
        ret = delete_dir_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(DELETE_DIR_TX), ret, thread_num);
        }
        break;
      case CREATE_DIR_NO_TX:
        ret = create_dir_no_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(CREATE_DIR_NO_TX), ret, thread_num);
        }
        break;
      case DELETE_DIR_NO_TX:
        ret = delete_dir_no_tx(dir, num_files, thread_num);
        if (ret) {
          return failure(STR_VALUE(DELETE_DIR_NO_TX), ret, thread_num);
        }
        break;
      case LIST_DIR_TX:
        ret = list_dir_tx(dir);
        if (ret) {
          return failure(STR_VALUE(LIST_DIR_TX), ret, thread_num);
        }
        break;
      case OPEN_NO_CLOSE:
        ret = open_no_close(dir);
        if (ret) {
          return failure(STR_VALUE(OPEN_NO_CLOSE), ret, thread_num);
        }
        break;
      default:
        fprintf(stderr, "Programmer error: Shouldn't make it to the default"
            " case.\n");
        return (void*)-1;
    }
  }

  return 0;
}


int main(int argc, char* const argv[]) {
  int ret, i, c, threads, ops, debug, pthread_ret, pthreads_failed = 0;
  unsigned seed;
  char cmd[128];
  pthread_t* read_threads = NULL;

  num_cores = sysconf(_SC_NPROCESSORS_ONLN);

  // Default arguments.
  threads = 2 * num_cores;
  seed = time(NULL);
  ops = 1;
  num_files = 5;
  debug = 0;
  // Do some minimal arg parsing.
  while ( (c = getopt(argc, argv, "t:s:n:f:odh")) != -1 ){
    switch(c) {
      case 't':
        threads = atoi(optarg);
        break;
      case 'f':
        num_files = atoi(optarg);
        break;
      case 's':
        seed = (unsigned)atoi(optarg);
        break;
      case 'n':
        if (!strcmp(optarg, "inf")) {
          ops = -1;
        }
        ops = atoi(optarg);
        break;
      case 'o':
        num_ops = NUM_OPS - NUM_DIR_OPS;
        break;
      case 'd':
        debug = 1;
        break;
      case 'h':
        printf("Usage: %s [-t NUM_THREADS (default is 2 * NUM_CORES)] "
            "[-s SEED (default is a random seed)] "
            "[-n NUM_OPERATIONS_PER_THREAD (default is 1)] "
            "[-f NUM_FILES (default is 5)] "
            "[-o (omit directory operations - only run file operations)] "
            "[-d (enable TX_FS debug messages)] "
            "[-h prints this message] \n"
            "\t -n inf will result in the threads running forever until an "
                "error occurs.\n", argv[0]);
        return 0;
        break;
      case '?':
        if (optopt == 't' || optopt == 's' || optopt == 'n') {
          fprintf (stderr, "Option -%c requires an argument.\n", optopt);
        } else {
          fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
        }
        return -1;
      default:
        abort();
    }
  }

  printf("Args: %d threads, %d seed, %d operations.\n", threads, seed, ops);

  srand(seed);

  read_threads = calloc(sizeof(pthread_t), threads);
  if (!read_threads) {
    fprintf(stderr, "Could not calloc %d number of pthread_t.\n", threads);
    return -1;
  }

  snprintf(dir, 63, "%s/testdir", PATH_VALUE(TESTDIR));
  snprintf(cmd, 127, "mkdir -p %s", dir);
  system(cmd);
  snprintf(cmd, 127, "mkdir %s/dir_open", dir);
  system(cmd);

  if(debug) {
    ret = syscall(TX_DEBUG_BEGIN);
    if (ret) {
      fprintf(stderr, "Could not start TX_FS debug (ret = %d): %s\n", ret,
          strerror(errno));
      return -1;
    }
  }

  /* Start threads. */
  for (i = 0; i < threads; i++) {
    arg_t *arg = malloc(sizeof(arg_t));
    if (!arg) {
      fprintf(stderr, "Could not malloc arg_t: %s.\n", strerror(errno));
      return -1;
    }
    arg->thread_num = i;
    arg->num_tasks = ops;
    ret = pthread_create(&(read_threads[i]), NULL, start_tasks, arg);

    if (ret) {
      fprintf(stderr, "Error starting thread %d: %s\n", i, strerror(errno));
      return -1;
    }
  }

  /* Wait for threads to stop. */
  for (i = 0; i < threads; i++) {
    pthread_ret = 0;
    ret = pthread_join(read_threads[i], (void**)(&pthread_ret));

    if (ret) {
      fprintf(stderr, "Error joining thread %d (ret = %d): %s\n", i, ret,
          strerror(errno));
    } else {
      printf("pthread %d finished\n", i);
    }

    if (pthread_ret) {
      pthreads_failed = 1;
    }
  }

  if(debug) {
    ret = syscall(TX_DEBUG_END);
    if (ret) {
      fprintf(stderr, "Could not end TX_FS debug (ret = %d): %s\n", ret,
          strerror(errno));
      return -1;
    }
  }

  return pthreads_failed;
}
