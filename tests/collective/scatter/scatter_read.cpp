#include <iostream>
#include <mpi.h>
#include "../../../capio_mpi/capio_mpi.hpp"
#include "../../common/utils.hpp"

int const NUM_ELEM = 100;

void compute_expected_result(int* expected_result, int array_length, int rank, int start) {
    for (int i = 0; i < array_length; ++i) {
        expected_result[i] = i + (array_length * rank) + start;
    }
}

int main(int argc, char** argv) {
    int* data, *expected_result;
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (argc != 3) {
        std::cout << "input error: number of producers and config file needed" << std::endl;
    }
    int num_prods = std::stoi(argv[1]);
    std::string config_path = argv[2];
    capio_mpi capio(size, num_prods, true, false, rank, config_path);
    std::cout << "reader " << rank << " before created capio object" << std::endl;
    if (NUM_ELEM % size == 0) {
        int array_length = NUM_ELEM / size;
        data = new int[array_length];
        expected_result = new int[array_length];
        capio.capio_scatter(nullptr, data, array_length);
        compute_expected_result(expected_result, array_length, rank, 0);
        compare_expected_actual(data, expected_result, array_length);
        capio.capio_scatter(nullptr, data, array_length);
        compute_expected_result(expected_result, array_length, rank, 1);
        compare_expected_actual(data, expected_result, array_length);
        free(data);
        free(expected_result);
    }
    std::cout << "reader " << rank << " ended " << std::endl;
    MPI_Finalize();
}