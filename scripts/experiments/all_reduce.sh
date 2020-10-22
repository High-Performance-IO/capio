#!/bin/bash

mpiexec -n 4 ../../build/experiments/files_comm/write_matrix_independent 10000 10000 > exp_all_reduce_prods.txt

mpiexec -n 4 ../../build/experiments/files_comm/read_all_reduce 10000 10000 > exp_all_reduce_cons.txt