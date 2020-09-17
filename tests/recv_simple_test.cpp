#include <iostream>
#include <mpi.h>
#include "../capio_mpi/capio_mpi.hpp"
#include "common/utils.hpp"

/*
 * test capio_recv. To use with send_simple_test.cpp
 *
 * each consumer sends an integer to a producer with the same rank
 *
 * send_simple_test and recv_simple_test must be lunched with an equals number of MPI processes
 *
 */

int main(int argc, char** argv) {
    int num;
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    capio_mpi capio(size, true, rank);
    std::cout << "reader " << rank << "created capio object" << std::endl;
    for (int i = 0; i < 100; ++i) {
        capio.capio_recv(&num, 1);
        compare_expected_actual(&num, &i, 1);
    }
    std::cout << "reader " << rank << "ended " << std::endl;
    MPI_Finalize();
}

