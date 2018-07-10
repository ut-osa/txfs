#!/bin/bash -f

echo "==============================="
echo "Running micro stress tests."

set -x

./stress_create0
./stress_write0

set +x
