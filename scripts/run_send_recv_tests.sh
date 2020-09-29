#!/bin/bash

mpiexec -n 2 ../build/capio_process/capio ../one_node_2_2.yaml > send_recv_f_l_2_2_capio.txt &
sleep .1
mpiexec -n 2 ../build/tests/recv_first_last_test ../one_node_2_2.yaml > send_recv_f_l_2_2_cons.txt &
sleep .1
mpiexec -n 2 ../build/tests/send_first_last_test 2 ../one_node_2_2.yaml > send_recv_f_l_2_2_prods.txt
if [ $? -ne 0 ];
then
    echo "all_gather test case 1 failed"
fi
sleep .1

mpiexec -n 2 ../build/capio_process/capio ../one_node_2_4.yaml > send_recv_f_l_2_4_capio.txt &
sleep .1
mpiexec -n 4 ../build/tests/recv_first_last_test ../one_node_2_4.yaml > send_recv_f_l_2_4_cons.txt &
sleep .1
mpiexec -n 2 ../build/tests/send_first_last_test 4 ../one_node_2_4.yaml > send_recv_f_l_2_4_prods.txt
if [ $? -ne 0 ];
then
    echo "all_gather test case 2 failed"
fi
sleep .1

mpiexec -n 2 ../build/capio_process/capio ../one_node_4_2.yaml > send_recv_f_l_4_2_capio.txt &
sleep .1
mpiexec -n 2 ../build/tests/recv_first_last_test ../one_node_4_2.yaml > send_recv_f_l_4_2_cons.txt &
sleep .1
mpiexec -n 4 ../build/tests/send_first_last_test 2 ../one_node_4_2.yaml > send_recv_f_l_4_2_prods.txt
if [ $? -ne 0 ];
then
    echo "all_gather test case 3 failed"
fi
sleep .1

mpiexec -n 2 ../build/capio_process/capio ../one_node_4_4.yaml > send_recv_f_l_4_2_capio.txt &
sleep .1
mpiexec -n 4 ../build/tests/recv_first_last_test ../one_node_4_4.yaml > send_recv_f_l_4_2_cons.txt &
sleep .1
mpiexec -n 4 ../build/tests/send_first_last_test 4 ../one_node_4_4.yaml > send_recv_f_l_4_2_prods.txt
if [ $? -ne 0 ];
then
    echo "all_gather test case 4 failed"
fi