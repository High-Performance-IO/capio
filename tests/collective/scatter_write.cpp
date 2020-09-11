#include <iostream>
#include <mpi.h>
#include "../../capio_mpi/capio_mpi.hpp"

int const NUM_ELEM = 100;

void initialize(int data[]) {
    for (int i = 0; i < NUM_ELEM; ++i) {
        data[i] = i;
    }
}

int main(int argc, char** argv) {
    int data[NUM_ELEM];
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (argc != 2) {
        std::cout << "input error: number of consumers needed " << std::endl;
        MPI_Finalize();
        return 0;
    }
    int num_cons = std::stoi(argv[1]);
    capio_mpi capio(num_cons, false);
    std::cout << "writer " << rank << "created capio object" << std::endl;
    initialize(data);
    if (rank == 0) {
        capio.capio_scatter(data, NUM_ELEM, nullptr, NUM_ELEM / num_cons);
    }
    std::cout << "writer " << rank << "ended " << std::endl;
    MPI_Finalize();
    return 0;
}