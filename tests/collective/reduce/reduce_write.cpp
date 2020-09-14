#include <iostream>
#include <mpi.h>
#include "../../../capio_mpi/capio_mpi.hpp"

int const NUM_ELEM = 100;

void initialize(int data[], int size, int num) {
    for (int i = 0; i < size; ++i) {
        data[i] = num;
        ++num;
    }
}

void sum(void* input_data, void* output_data, int* count, MPI_Datatype* data_type) {
    int* input = (int*)input_data;
    int* output = (int*)output_data;

    for(int i = 0; i < *count; i++) {
        output[i] += input[i];
    }
}

int main(int argc, char** argv) {
    int* data;
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
    data = new int[NUM_ELEM];
    initialize(data, NUM_ELEM, 0);
    capio.capio_reduce(data, nullptr, NUM_ELEM, MPI_INT, sum, 0, rank);
    capio.capio_reduce(data, nullptr, NUM_ELEM, MPI_INT, sum, 0, rank);
    free(data);
    std::cout << "writer " << rank << "ended " << std::endl;
    MPI_Finalize();
    return 0;
}