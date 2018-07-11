CC = gcc
CXX = g++
TEST_DIR="/data/test"
DEFS="-DTESTDIR=$(TEST_DIR)"

PROGS = example

all: $(PROGS)

example: syscall_wrapper.h example.c
	$(CC) $(DEFS) -o example example.c
clean:
	rm -f *.o *~ $(PROGS) file*
