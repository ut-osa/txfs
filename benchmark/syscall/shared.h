#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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

#define DEFINE_1_FILE				\
	FILE_DEFINE(0);
