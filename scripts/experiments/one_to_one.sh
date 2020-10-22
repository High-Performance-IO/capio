#!/bin/bash



mpiexec -n 4 ../../build/experiments/files_comm/write_one_matrix 100 100 > exp_read_one_4_4_prods.txt

mpiexec -n 4 ../../build/experiments/files_comm/read_one_matrix 100 100 > exp_write_one_4_4_cons.txt
