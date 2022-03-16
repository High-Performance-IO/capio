#!/bin/bash


if [ $# -lt 3 ] || [ $# -gt 4 ]
then
	echo "input error: ./test_capio_streaming_pipeline.sh parallelism num_elements num_IO_ops [MPI params])"
exit
fi

PAR=$1
NUM_ELEMS=$2
N_IOOPS=$3
MPI_PARS=$4
echo $PAR " " $NUM_ELEMS " " $N_IOOPS >> time_streaming_pipeline.txt
mpiexec $MPI_PARS -n $PAR unit_tests/simple_write_nc $NUM_ELEMS $N_IOOPS > output_writes.txt
mpiexec $MPI_PARS -n $PAR unit_tests/simple_read_nc $NUM_ELEMS $N_IOOPS > output_reads.txt
