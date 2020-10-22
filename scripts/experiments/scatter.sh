#!/bin/bash

mpiexec -n 4 ../../build/experiments/files_comm/write_scatter 100 100 > write_scatter_prods.txt

mpiexec -n 4 ../../build/experiments/files_comm/read_scatter 100 100 > read_scatter_cons.txt