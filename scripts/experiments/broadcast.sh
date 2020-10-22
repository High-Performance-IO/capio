#!/bin/bash

mpiexec -n 4 ../../build/experiments/files_comm/write_broadcast 100 100 > write_broadcast_prods.txt

mpiexec -n 4 ../../build/experiments/files_comm/read_broadcast 100 100 > read_broadcast_cons.txt