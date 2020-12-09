#include <iostream>
#include <mpi.h>
#include "../capio_ordered/capio_ordered.hpp"
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
    int rank;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (argc != 3) {
        std::cout << "input error: capio buffer size and config file nedded" << std::endl;
        MPI_Finalize();
        return 1;
    }
    int buf_size = std::stoi(argv[1]);
    std::string config_path = argv[2];
    capio_ordered capio(true, false, rank, buf_size, config_path);
    std::cout << "reader " << rank << "created capio object" << std::endl;
    for (int i = 0; i < 100; ++i) {
        capio.capio_recv(&num, 1);
        compare_expected_actual(&num, &i, 1);
    }
    std::cout << "reader " << rank << "ended " << std::endl;
    MPI_Finalize();
    return 0;
}

