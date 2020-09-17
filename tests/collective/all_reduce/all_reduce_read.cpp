#include <iostream>
#include <mpi.h>
#include "../../../capio_mpi/capio_mpi.hpp"
#include "../../common/utils.hpp"
int const NUM_ELEM = 100;



void compute_expected_result(int* expected_result, int array_length, int num_prods, int start) {
    for (int i = 0; i < array_length; ++i) {
        expected_result[i] = 0;
        for (int j = 0; j < num_prods; ++j) {
            expected_result[i] += j + start + i;
        }
    }
}




int main(int argc, char** argv) {
    int* data, *expected_result;
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    capio_mpi capio(size, true, rank);
    std::cout << "reader " << rank << " before created capio object" << std::endl;
    if (argc != 2) {
        std::cout << "input error: number of producers needed " << std::endl;
        MPI_Finalize();
        return 0;
    }
    int num_prods= std::stoi(argv[1]);
    if (NUM_ELEM % size == 0) {
        data = new int[NUM_ELEM];
        expected_result = new int[NUM_ELEM];
        capio.capio_all_reduce(nullptr, data, NUM_ELEM, MPI_INT, nullptr, -1);
        compute_expected_result(expected_result, NUM_ELEM, num_prods, 0);
        compare_expected_actual(data, expected_result, NUM_ELEM);
        capio.capio_all_reduce(nullptr, data, NUM_ELEM, MPI_INT, nullptr, -1);
        compute_expected_result(expected_result, NUM_ELEM, num_prods, 1);
        compare_expected_actual(data, expected_result, NUM_ELEM);
        free(data);
        free(expected_result);
    }
    std::cout << "reader " << rank << " ended " << std::endl;
    MPI_Finalize();
}