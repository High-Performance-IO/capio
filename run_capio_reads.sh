#!/bin/bash

LD_LIBRARY_PATH=. mpiexec --mca btl_openib_allow_ib 1 --hostfile $4 -n $1 ./unit_tests/simple_read $2 $3 time_capio_reads_remote.txt > output_reads_capio.txt
