#!/bin/bash


mpiexec --mca btl_openib_allow_ib 1 --hostfile $4 -n $1 unit_tests/simple_write_nc $2 $3 time_writes_remote.txt  > output_writes.txt
