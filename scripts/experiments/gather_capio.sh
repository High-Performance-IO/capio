#!/bin/bash

mpiexec -n 2 ../../build/capio_process/capio 100000010 ../../one_node_4_4.yaml > exp_capio_read_to_one_4_4_capio.txt &

mpiexec -n 4 ../../build/experiments/capio_comm/capio_write_gather 2000 10000 ../../one_node_4_4.yaml > exp_capio_read_to_one_4_4_prods.txt &

mpiexec -n 4 ../../build/experiments/capio_comm/capio_read_gather 2000 10000 ../../one_node_4_4.yaml > exp_capio_read_to_one_4_4_cons.txt

