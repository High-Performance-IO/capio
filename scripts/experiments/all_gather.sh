#!/bin/bash

mpiexec -n 4 ../../build/experiments/files_comm/write_matrix_independent 100 100 > exp_all_gather_prods.txt

mpiexec -n 4 ../../build/experiments/files_comm/read_all_gather 100 100 > exp_all_gather_cons.txt