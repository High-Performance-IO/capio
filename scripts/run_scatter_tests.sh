#!/bin/bash

mpiexec -n 2 ../build/capio_process/capio 2 ../one_node_2_2.yaml > scatter_2_2_capio.txt &
sleep .1
mpiexec -n 2 ../build/tests/collective/scatter/scatter_read ../one_node_2_2.yaml > scatter_2_2_cons.txt &
sleep .1
mpiexec -n 2 ../build/tests/collective/scatter/scatter_write 2 ../one_node_2_2.yaml > scatter_2_2_prods.txt
if [ $? -ne 0 ];
then
    echo "scatter test case 1 failed"
fi

mpiexec -n 2 ../build/capio_process/capio 4 ../one_node_2_4.yaml > scatter_2_4_capio.txt &
sleep .1
mpiexec -n 4 ../build/tests/collective/scatter/scatter_read ../one_node_2_4.yaml > scatter_2_4_cons.txt &
sleep .1
mpiexec -n 2 ../build/tests/collective/scatter/scatter_write 4 ../one_node_2_4.yaml > scatter_2_4_prods.txt
if [ $? -ne 0 ];
then
    echo "scatter test case 2 failed"
fi

mpiexec -n 2 ../build/capio_process/capio 2 ../one_node_4_2.yaml > scatter_4_2_capio.txt &
sleep .1
mpiexec -n 2 ../build/tests/collective/scatter/scatter_read ../one_node_4_2.yaml > scatter_4_2_cons.txt &
sleep .1
mpiexec -n 4 ../build/tests/collective/scatter/scatter_write 2 ../one_node_4_2.yaml > scatter_4_2_prods.txt
if [ $? -ne 0 ];
then
    echo "scatter test case 3 failed"
fi