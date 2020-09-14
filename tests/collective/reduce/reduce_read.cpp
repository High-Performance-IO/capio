#include <iostream>
#include <mpi.h>
#include "../../../capio_mpi/capio_mpi.hpp"

int const NUM_ELEM = 100;

void print_array(int data[], int array_length, int rank) {
    std::cout << "array_length: " << array_length << std::endl;
    for (int i = 0; i < array_length; ++i) {
        std::cout << "reader " << rank << " data: " << data[i] << std::endl;
    }
}

int main(int argc, char** argv) {
    int* data;
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    capio_mpi capio(size, true, rank);
    std::cout << "reader " << rank << " before created capio object" << std::endl;
    data = new int[NUM_ELEM];
    capio.capio_reduce(nullptr, data, NUM_ELEM, MPI_INT, nullptr, 0, 0);
    if (rank == 0) {
        print_array(data, NUM_ELEM, rank);
    }
    free(data);
    std::cout << "reader " << rank << " ended " << std::endl;
    MPI_Finalize();
}