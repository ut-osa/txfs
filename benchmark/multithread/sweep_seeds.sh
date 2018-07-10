#! /usr/bin/sudo /bin/bash

# Run fairly lightweight just so we can find quickly-occuring errors.
THREADS=2
OPS=100
NUM_SEEDS=10
N=0
OUTPUT_FILE=out.txt

[ -f ./multi-random ] || make

for i in $(seq 1 $NUM_SEEDS); do
  RAND=$(od -A n -t d -N 4 /dev/urandom)
  echo "Running seed = $RAND"
  ./multi-random -o -n $OPS -t $THREADS -s $RAND > $OUTPUT_FILE 2>&1 || { echo "$RAND fails"; ((N=N+1)); }
done

echo "$N of $NUM_SEEDS failed."

