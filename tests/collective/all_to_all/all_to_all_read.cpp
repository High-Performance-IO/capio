#include <iostream>
#include <mpi.h>
#include "../../../capio_mpi/capio_mpi.hpp"
#include "../../common/utils.hpp"

int const NUM_ELEM = 100;

void compute_expected_result(int* expected_result, int array_length, int size, int rank) {
    int start = array_length / size * rank;
    for (int i = 0, num = start; i < array_length; ++i) {
        expected_result[i] = num;
        ++num;
        if ((i + 1) % (array_length / size) == 0) {
            ++start;
            num = start;
        }
    }
}

int main(int argc, char** argv) {
    int* data, *expected_result;
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (argc != 2) {
        std::cout << "input error: number of producers needed " << std::endl;
        MPI_Finalize();
        return 0;
    }
    int num_prods= std::stoi(argv[1]);
    capio_mpi capio(size, true, rank);
    std::cout << "reader " << rank << " before created capio object" << std::endl;
    data = new int[NUM_ELEM];
    expected_result = new int[NUM_ELEM];
    capio.capio_all_to_all(nullptr, NUM_ELEM / size, data, num_prods);
    compute_expected_result(expected_result, NUM_ELEM, size, rank);
    compare_expected_actual(data, expected_result, NUM_ELEM);
    capio.capio_all_to_all(nullptr, NUM_ELEM / size, data, num_prods);
    compare_expected_actual(data, expected_result, NUM_ELEM);
    free(data);
    free(expected_result);
    std::cout << "reader " << rank << " ended " << std::endl;
    MPI_Finalize();
}