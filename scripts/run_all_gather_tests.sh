#!/bin/bash

mpiexec -n 2 ../build/capio_process/capio 1048576 10000 ../one_node_2_2.yaml > all_gather_2_2_capio.txt &
sleep .1
mpiexec -n 2 ../build/tests/collective/all_gather/all_gather_read 1048576 2 ../one_node_2_2.yaml > all_gather_2_2_cons.txt &
sleep .1
mpiexec -n 2 ../build/tests/collective/all_gather/all_gather_write 1048576 2 ../one_node_2_2.yaml > all_gather_2_2_prods.txt
if [ $? -ne 0 ];
then
    echo "all_gather test case 1 failed"
fi
rm /dev/shm/*

