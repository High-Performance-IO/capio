#!/bin/bash

mpiexec -n 2 ../../build/capio_process/capio 10010 ../../one_node_4_4.yaml > capio_write_read_capio.txt &

mpiexec -n 4 ../../build/experiments/capio_comm/capio_write_matrixes 100 100 ../../one_node_4_4.yaml > capio_write_read_prods.txt &

mpiexec -n 4 ../../build/experiments/capio_comm/capio_read_matrixes 100 100 ../../one_node_4_4.yaml > capio_write_read_cons.txt

