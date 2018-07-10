CC = gcc
CXX = g++
TEST_DIR="/data/test"
DEFS="-DTESTDIR=$(TEST_DIR)"

PROGS = start end create0 create1 create2 create3 \
	write0 write1 write2 write3 \
	read0 close-abort close-commit fstat0 \
	unlink0 unlink1 unlink2 unlink3 unlink4 \
	conflict0 conflict1 \
	test test2 dir0

all: $(PROGS)

test: shared.h test.c
	$(CC) $(DEFS) -o test test.c

create0: shared.h create0.c
	$(CC) $(DEFS) -o create0 create0.c
create1: shared.h create1.c
	$(CC) $(DEFS) -o create1 create1.c
create2: shared.h create2.c
	$(CC) $(DEFS) -o create2 create2.c
create3: shared.h create3.c
	$(CC) $(DEFS) -o create3 create3.c

write0: shared.h write0.c
	$(CC) $(DEFS) -o write0 write0.c
write1: shared.h write1.c
	$(CC) $(DEFS) -o write1 write1.c
write2: shared.h write2.c
	$(CC) $(DEFS) -o write2 write2.c
write3: shared.h write3.c
	$(CC) $(DEFS) -o write3 write3.c

dir0: shared.h dir0.c
	$(CC) $(DEFS) -o dir0 dir0.c

unlink0: shared.h unlink0.c
	$(CC) $(DEFS) -o unlink0 unlink0.c

unlink1: shared.h unlink1.c
	$(CC) $(DEFS) -o unlink1 unlink1.c

unlink2: shared.h unlink2.c
	$(CC) $(DEFS) -o unlink2 unlink2.c

unlink3: shared.h unlink3.c
	$(CC) $(DEFS) -o unlink3 unlink3.c

unlink4: shared.h unlink4.c
	$(CC) $(DEFS) -o unlink4 unlink4.c

start: start.c
	$(CC) $(DEFS) -o start start.c

end: end.c
	$(CC) $(DEFS) -o end end.c

read0: read0.c
	dd if=/dev/urandom of=$(TEST_DIR)/read.t bs=4096 count=2
	$(CC) $(DEFS) -o read0 read0.c

close-abort: close-abort.c
	$(CC) $(DEFS) -o close-abort close-abort.c
close-commit: close-commit.c
	$(CC) $(DEFS) -o close-commit close-commit.c

clean:
	rm -f test *.o *~ read.t $(PROGS)
