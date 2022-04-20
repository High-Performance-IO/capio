#!/bin/bash

if [ $# -ne 2 ]
then
	echo "input error: ./run_rw_test_remote.sh num_elems num_IO_ops"
exit
fi

NUM_INTS=$1
NUM_IO=$2

echo $NUM_INTS $NUM_IO >> time_writes_remote.txt
echo $NUM_INTS $NUM_IO >> time_reads_remote.txt
for P in 1 2 4 8 16 24
do
	echo $P >> time_writes_remote.txt
	echo $P >> time_reads_remote.txt
	{ time ./run_writes.sh $P $NUM_INTS $NUM_IO hostfile_write ; } 2> >(grep real >> time_writes_remote.txt)
	{ time ./run_reads.sh $P $NUM_INTS $NUM_IO hostfile_read ; } 2> >(grep real >> time_reads_remote.txt)
done
