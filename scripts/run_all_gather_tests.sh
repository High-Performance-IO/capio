#!/bin/bash

mpiexec -n 2 ../build/capio_process/capio 2 2 ../one_node_2_2.yaml > all_gather_2_2_capio.txt &
sleep .1
mpiexec -n 2 ../build/tests/collective/all_gather/all_gather_read 2 ../one_node_2_2.yaml > all_gather_2_2_cons.txt &
sleep .1
mpiexec -n 2 ../build/tests/collective/all_gather/all_gather_write 2 ../one_node_2_2.yaml > all_gather_2_2_prods.txt
if [ $? -ne 0 ];
then
    echo "all_gather test case 1 failed"
fi
sleep .1

mpiexec -n 2 ../build/capio_process/capio 4 2 ../one_node_2_4.yaml > all_gather_2_4_capio.txt &
sleep .1
mpiexec -n 4 ../build/tests/collective/all_gather/all_gather_read 2 ../one_node_2_4.yaml > all_gather_2_4_cons.txt &
sleep .1
mpiexec -n 2 ../build/tests/collective/all_gather/all_gather_write 4 ../one_node_2_4.yaml > all_gather_2_4_prods.txt
if [ $? -ne 0 ];
then
    echo "all_gather test case 2 failed"
fi
sleep .1

mpiexec -n 2 ../build/capio_process/capio 2 4 ../one_node_4_2.yaml > all_gather_4_2_capio.txt &
sleep .1
mpiexec -n 2 ../build/tests/collective/all_gather/all_gather_read 4 ../one_node_4_2.yaml > all_gather_4_2_cons.txt &
sleep .1
mpiexec -n 4 ../build/tests/collective/all_gather/all_gather_write 2 ../one_node_4_2.yaml > all_gather_4_2_prods.txt
if [ $? -ne 0 ];
then
    echo "all_gather test case 3 failed"
fi