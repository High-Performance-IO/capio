#!/bin/bash

mpiexec -n 2 ../build/capio_process/capio 1048576 10000 ../one_node_2_2.yaml > send_recv_f_l_2_2_capio.txt &
sleep .1
mpiexec -n 2 ../build/tests/recv_first_last_test 1048576 ../one_node_2_2.yaml > send_recv_f_l_2_2_cons.txt &
sleep .1
mpiexec -n 2 ../build/tests/send_first_last_test 1048576 2 ../one_node_2_2.yaml > send_recv_f_l_2_2_prods.txt
if [ $? -ne 0 ];
then
    echo "all_gather test case 1 failed"
fi
sleep .1
