#!/bin/bash



mpiexec -n 4 ../../build/experiments/files_comm/write_matrix_independent 2000 10000 > exp_parallel_reads_to_one_4_4_prods.txt

mpiexec -n 4 ../../build/experiments/files_comm/gather_read_exp 2000 10000 > exp_parallel_reads_to_one_4_4_cons.txt
