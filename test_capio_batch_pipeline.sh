#!/bin/bash

if [ $# -lt 3 ] || [ $# -gt 4 ]
then
	echo "input error: ./test_capio_batch_pipeline.sh parallelism num_elements num_IO_ops [MPI params])"
exit
fi

PAR=$1
NUM_ELEMS=$2
N_IOOPS=$3
MPI_PARS=$4
mpiexec --hostfile hostfile_server $MPI_PARS -N 1 src/capio_server > server.log &
PID=$!
{ time mpiexec --hostfile hostfile_write $MPI_PARS -n $PAR unit_tests/simple_write $NUM_ELEMS $N_IOOPS time_capio_writes.txt > output_capio_writes.txt ; } 2> >(grep real >> time_capio_writes.txt) 
{ time mpiexec --hostfile hostfile_read $MPI_PARS -n $PAR unit_tests/simple_read $NUM_ELEMS $N_IOOPS time_capio_reads.txt > output_capio_reads.txt ; } 2> >(grep real >> time_capio_reads.txt) 
kill $PID
sleep 5
./clean.sh
