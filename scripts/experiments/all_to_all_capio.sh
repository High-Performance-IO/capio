#!/bin/bash

mpiexec -n 2 ../../build/capio_process/capio 100000010 ../../one_node_4_4.yaml > all_to_all_capio.txt &

mpiexec -n 4 ../../build/experiments/capio_comm/capio_write_all_to_all 10000 10000 ../../one_node_4_4.yaml > capio_all_to_all_prods.txt &

mpiexec -n 4 ../../build/experiments/capio_comm/capio_read_all_to_all 10000 10000 ../../one_node_4_4.yaml > capio_all_to_all_cons.txt

