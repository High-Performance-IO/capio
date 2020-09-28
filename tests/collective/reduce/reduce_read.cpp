#include <iostream>
#include <mpi.h>
#include "../../../capio_mpi/capio_mpi.hpp"
#include "../../common/utils.hpp"

int const NUM_ELEM = 100;


int main(int argc, char** argv) {
    int data[NUM_ELEM];
    int expected_result[NUM_ELEM];
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (argc != 3) {
        std::cout << "input error: number of consumers and config file needed " << std::endl;
        MPI_Finalize();
        return 0;
    }
    int num_prods = std::stoi(argv[1]);
    std::string config_path = argv[2];
    std::cout << "reader " << rank << " before created capio object" << std::endl;
    capio_mpi capio(size, num_prods, true, false, rank, config_path);
    capio.capio_reduce(nullptr, data, NUM_ELEM, MPI_INT, nullptr, 0, 0);
    if (rank == 0) {
        compute_expected_result_reduce(expected_result, NUM_ELEM, num_prods, 0);
        compare_expected_actual(data, expected_result, NUM_ELEM);
    }
    capio.capio_reduce(nullptr, data, NUM_ELEM, MPI_INT, nullptr, 0, 0);
    if (rank == 0) {
        compute_expected_result_reduce(expected_result, NUM_ELEM, num_prods, 1);
        compare_expected_actual(data, expected_result, NUM_ELEM);
    }
    std::cout << "reader " << rank << " ended " << std::endl;
    MPI_Finalize();
}