#!/bin/bash

mpiexec -n 2 ../../build/capio_process/capio 10010 ../../one_node_4_4.yaml > all_gather_capio.txt &

mpiexec -n 4 ../../build/experiments/capio_comm/capio_write_all_gather 100 100 ../../one_node_4_4.yaml > capio_read_all_gather_prods.txt &

mpiexec -n 4 ../../build/experiments/capio_comm/capio_read_all_gather 100 100 ../../one_node_4_4.yaml > capio_read_all_gathere_cons.txt

