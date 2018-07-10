#!/bin/bash -f

echo "==============================="
echo "Running multi-threaded tests."

set -x

./multi-write0
./multi-rw0
./multi-rw1
./multi-rw2
./multi-create_no_conflict
./multi-create_conflict
./multi-read_isolation0
./multi-write_block_no_conflict
./multi-sync

set +x
