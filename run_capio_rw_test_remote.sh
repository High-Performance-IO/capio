#!/bin/bash

if [ $# -ne 2 ]
then
	echo "input error: ./run_capio_rw_test_remote.sh num_elems num_IO_ops"
exit
fi

NUM_INTS=$1
NUM_IO=$2

echo $NUM_INTS $NUM_IO >> time_capio_writes_remote.txt
echo $NUM_INTS $NUM_IO >> time_capio_reads_remote.txt
for P in 1 2 4 8 16 24
do
	echo $P >> time_capio_writes_remote.txt
	echo $P >> time_capio_reads_remote.txt
	mpiexec --mca btl_openib_allow_ib 1 --hostfile hostfile_server -N 1 src/capio_server &
	PID=$!
	{ time ./run_capio_writes.sh $P $NUM_INTS $NUM_IO hostfile_write ; } 2> >(grep real >> time_capio_writes_remote.txt)
	{ time ./run_capio_reads.sh $P $NUM_INTS $NUM_IO hostfile_read ; } 2> >(grep real >> time_capio_reads_remote.txt)
	kill $PID
	sleep 5
done
