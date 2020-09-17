#include <iostream>
#include <mpi.h>
#include "../../../capio_mpi/capio_mpi.hpp"
#include "../../common/utils.hpp"

int const NUM_ELEM = 100;


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
        int array_length = NUM_ELEM / size * num_prods;
        data = new int[array_length];
        expected_result = new int[array_length];
        capio.capio_all_gather(nullptr, 0, data, array_length);
        compute_expected_result_gather(expected_result, array_length, num_prods, 0);
        compare_expected_actual(data, expected_result, array_length);
        capio.capio_all_gather(nullptr, 0, data, array_length);
        compute_expected_result_gather(expected_result, array_length, num_prods, 1);
        compare_expected_actual(data, expected_result, array_length);
        free(data);
        free(expected_result);
    }
    std::cout << "reader " << rank << " ended " << std::endl;
    MPI_Finalize();
}