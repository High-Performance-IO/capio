#include <iostream>
#include <mpi.h>
#include "../../../capio_mpi/capio_mpi.hpp"
#include "../../common/utils.hpp"

int const NUM_ELEM = 100;

int main(int argc, char** argv) {
    int* data;
    int rank;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (argc != 3) {
        std::cout << "input error: config file needed " << std::endl;
        MPI_Finalize();
        return 0;
    }
    std::string config_path = argv[2];
    capio_mpi capio(false, true, rank, config_path);
    std::cout << "writer " << rank << "created capio object" << std::endl;
    data = new int[NUM_ELEM];
    initialize(data, NUM_ELEM, rank);
    capio.capio_broadcast(data, NUM_ELEM, 0);
    capio.capio_broadcast(data, NUM_ELEM, 0);
    free(data);
    std::cout << "writer " << rank << "ended " << std::endl;
    MPI_Finalize();
    return 0;
}