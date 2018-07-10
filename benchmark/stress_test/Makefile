CC = gcc
CXX = g++
TEST_DIR="/data/test"
DEFS=-DTESTDIR=$(TEST_DIR)

PROGS=stress_create0 stress_write0

all: $(PROGS)

clean: 
	rm -f test *.o *~ read.t $(PROGS)

stress_create0: stress_create0.c shared.h
	$(CC) stress_create0.c $(DEFS) -o stress_create0

stress_write0: stress_write0.c shared.h
	$(CC) stress_write0.c $(DEFS) -o stress_write0

#%: %.c share.h
#	$(CC) $^ $(DEFS) -o $@
