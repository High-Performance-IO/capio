#include <iostream>
#include <mpi.h>
#include "../../../capio_mpi/capio_mpi.hpp"
#include "../../common/utils.hpp"

int const NUM_ELEM = 100;

int main(int argc, char** argv) {
    int data[NUM_ELEM];
    int rank;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (argc != 3) {
        std::cout << "input error: number of consumers and config file needed " << std::endl;
        MPI_Finalize();
        return 0;
    }
    int num_cons = std::stoi(argv[1]);
    std::string config_path = argv[2];
    capio_mpi capio(false, true, rank, config_path);
    std::cout << "writer " << rank << "created capio object" << std::endl;

    if (rank == 0) {
        initialize(data, NUM_ELEM, 0);
        capio.capio_scatter(data, nullptr, NUM_ELEM / num_cons);
        initialize(data, NUM_ELEM, 1);
        capio.capio_scatter(data, nullptr, NUM_ELEM / num_cons);
    }
    std::cout << "writer " << rank << "ended " << std::endl;
    MPI_Finalize();
    return 0;
}