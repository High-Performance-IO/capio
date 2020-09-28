#include <iostream>
#include <mpi.h>
#include "../../../capio_mpi/capio_mpi.hpp"
#include "../../common/utils.hpp"

int const NUM_ELEM = 100;


void compute_expected_result(int data[], int size) {
    for (int i = 0; i < size; ++i) {
        data[i] = i;
    }
}

int main(int argc, char** argv) {
    int data[NUM_ELEM]{0};
    int expected_result[NUM_ELEM];
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (argc != 3) {
        std::cout << "input error: number of producers and config file needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    std::cout << "argv[1] " << argv[1] << std::endl;
    int num_prods = std::stoi(argv[1]);
    std::string config_path = argv[2];
    capio_mpi capio(size, num_prods, true, false, rank, config_path);
    std::cout << "reader " << rank << " before created capio object" << std::endl;
    compute_expected_result(expected_result, NUM_ELEM);
    capio.capio_broadcast(data, NUM_ELEM, 0);
    compare_expected_actual(data, expected_result, NUM_ELEM);
    capio.capio_broadcast(data, NUM_ELEM, 0);
    compare_expected_actual(data, expected_result, NUM_ELEM);
    std::cout << "reader " << rank << " ended " << std::endl;
    MPI_Finalize();
    return 0;
}