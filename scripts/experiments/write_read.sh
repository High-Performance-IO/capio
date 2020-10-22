#!/bin/bash

mpiexec -n 4 ../../build/experiments/files_comm/write_matrix_independent 100 100 > write_read_prods.txt

mpiexec -n 4 ../../build/experiments/files_comm/read_matrixes 100 100 > write_read_cons.txt