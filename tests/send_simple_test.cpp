#include <iostream>
#include <mpi.h>
#include "../capio_mpi/capio_mpi.hpp"

/*
 * test capio_send. To use with recv_simple_test.cpp
 *
 * each consumer sends an integer to a producer with the same rank
 *
 * send_simple_test and recv_simple_test must be lunched with an equals number of MPI processes
 *
 */

int main(int argc, char** argv) {
    int num = 0;
    int rank, recipient_rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    capio_mpi capio(size, false);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::cout << "writer " << rank << "created capio object" << std::endl;
    recipient_rank = rank;
    for (int i = 0; i < 100; ++i) {
        std::cout << "writer " << rank << ": " << num << std::endl;
        capio.capio_send(&num, 1, recipient_rank);
        ++num;
    }
    std::cout << "writer " << rank << "ended " << std::endl;
    MPI_Finalize();
}


