#! /bin/bash -f

set -x

for i in `seq 0 3`;
do
	sync
	echo 3 > /proc/sys/vm/drop_caches
	./create$i
done

for i in `seq 3 -1 0`;
do
	sync
	echo 3 > /proc/sys/vm/drop_caches
	./create$i
done

for i in `seq 0 3`;
do
	sync
	echo 3 > /proc/sys/vm/drop_caches
	./create$i
done

for i in `seq 0 3`;
do
	sync
	echo 3 > /proc/sys/vm/drop_caches
	./write$i
done

for i in `seq 3 -1 0`;
do
	sync
	echo 3 > /proc/sys/vm/drop_caches
	./write$i
done

for i in `seq 0 4`;
do
	sync
	echo 3 > /proc/sys/vm/drop_caches
	./unlink$i
done

for i in `seq 4 -1 0`;
do
	sync
	echo 3 > /proc/sys/vm/drop_caches
	./unlink$i
done


sync
echo 3 > /proc/sys/vm/drop_caches
./dir0

set +x
