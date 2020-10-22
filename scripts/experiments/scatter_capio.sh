#!/bin/bash

mpiexec -n 2 ../../build/capio_process/capio 10010 ../../one_node_4_4.yaml > exp_capio_read_to_one_4_4_capio.txt &

mpiexec -n 4 ../../build/experiments/capio_comm/write_scatter_exp 100 100 ../../one_node_4_4.yaml > capio_read_scatter_4_4_prods.txt &

mpiexec -n 4 ../../build/experiments/capio_comm/read_scatter_exp 100 100 ../../one_node_4_4.yaml > capio_write_scatter_4_4_cons.txt

