CC = gcc
CXX = g++
# https://bugs.launchpad.net/ubuntu/+source/glibc/+bug/1427981
# Stack protector apparently causes issues with pthread_join.
GCC_FLAGS=-fno-stack-protector
CFLAGS=-lpthread -lrt
TEST_DIR="/data/test"
DEFS=-DTESTDIR=$(TEST_DIR)

PROGS=multi-write0 multi-rw0 multi-rw1 multi-rw2 \
	multi-create_no_conflict multi-create_conflict \
	multi-read_isolation0 multi-read_isolation1 multi-read_isolation2 \
	multi-read_isolation3 multi-read_isolation4 multi-read_isolation5 \
	multi-write_block_no_conflict \
	multi-sync multi-random mmap-transaction mmap-conflict \
	list-dir

all: $(PROGS)

clean:
	rm -f *.o $(PROGS)

print-%: ; @echo $($*)

multi-write0: multi-write0.o
	$(CC) -o $@ $< $(CFLAGS)

multi-write0.o: multi-write0.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-rw0: multi-rw0.o
	$(CC) -o $@ $< $(CFLAGS)

multi-rw0.o: multi-rw0.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-rw1: multi-rw1.o
	$(CC) -o $@ $< $(CFLAGS)

multi-rw1.o: multi-rw1.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-rw2: multi-rw2.o
	$(CC) -o $@ $< $(CFLAGS)

multi-rw2.o: multi-rw2.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-create_no_conflict: multi-create_no_conflict.o
	$(CC) -o $@ $< $(CFLAGS)

multi-create_no_conflict.o: multi-create_no_conflict.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-create_conflict: multi-create_conflict.o
	$(CC) -o $@ $< $(CFLAGS)

multi-create_conflict.o: multi-create_conflict.c shared.h shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-read_isolation0: multi-read_isolation0.o
	$(CC) -o $@ $< $(CFLAGS)

multi-read_isolation0.o: multi-read_isolation0.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-read_isolation1: multi-read_isolation1.o
	$(CC) -o $@ $< $(CFLAGS)

multi-read_isolation1.o: multi-read_isolation1.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-read_isolation2: multi-read_isolation2.o
	$(CC) -o $@ $< $(CFLAGS)

multi-read_isolation2.o: multi-read_isolation2.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-read_isolation3: multi-read_isolation3.o
	$(CC) -o $@ $< $(CFLAGS)

multi-read_isolation3.o: multi-read_isolation3.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-read_isolation4: multi-read_isolation4.o
	$(CC) -o $@ $< $(CFLAGS)

multi-read_isolation4.o: multi-read_isolation4.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-read_isolation5: multi-read_isolation5.o
	$(CC) -o $@ $< $(CFLAGS)

multi-read_isolation5.o: multi-read_isolation5.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-write_block_no_conflict: multi-write_block_no_conflict.o
	$(CC) -o $@ $< $(CFLAGS)

multi-write_block_no_conflict.o: multi-write_block_no_conflict.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-sync: multi-sync.o
	$(CC) -o $@ $< $(CFLAGS)

multi-sync.o: multi-sync.c shared.h
	$(CC) -o $@ $< -c $(DEFS)

multi-random: multi-random.o
	$(CC) -o $@ $< $(CFLAGS)

multi-random.o: multi-random.c shared.h
	$(CC) -o $@ $(GCC_FLAGS) $< -c $(DEFS)

mmap-transaction: mmap-transaction.o
	$(CC) -o $@ $< $(CFLAGS)

mmap-transaction.o: mmap-transaction.c shared.h
	$(CC) -g -o $@ $< -c $(DEFS)

mmap-conflict: mmap-conflict.o
	$(CC) -o $@ $< $(CFLAGS)

mmap-conflict.o: mmap-conflict.c shared.h
	$(CC) -o $@ $(GCC_FLAGS) $< -c $(DEFS)

list-dir: list-dir.o
	$(CC) -o $@ $< $(CFLAGS)

list-dir.o: list-dir.c shared.h
	$(CC) -o $@ $(GCC_FLAGS) $< -c $(DEFS)
