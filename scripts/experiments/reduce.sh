#!/bin/bash



mpiexec -n 4 ../../build/experiments/files_comm/write_matrix_independent 100 100 > exp_reduce_4_4_prods.txt

mpiexec -n 4 ../../build/experiments/files_comm/reduce_matrix_independent_one_matrix 100 100 > exp_reduce_4_4_cons.txt