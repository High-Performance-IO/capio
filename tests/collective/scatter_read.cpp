#include <iostream>
#include <mpi.h>
#include "../../capio_mpi/capio_mpi.hpp"

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
    if (NUM_ELEM % size == 0) {
        int array_length = NUM_ELEM / size;
        data = new int[array_length];
        capio.capio_scatter(nullptr, 0, data, array_length);
        print_array(data, array_length, rank);
        capio.capio_scatter(nullptr, 0, data, array_length);
        print_array(data, array_length, rank);
    }
    std::cout << "reader " << rank << " ended " << std::endl;
    MPI_Finalize();
}