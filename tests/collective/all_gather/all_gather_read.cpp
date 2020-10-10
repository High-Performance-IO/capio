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
    if (argc != 3) {
        std::cout << "input error: number of producers and config file needed " << std::endl;
        MPI_Finalize();
        return 0;
    }
    int num_prods= std::stoi(argv[1]);
    std::string config_path = argv[2];
    std::cout << "reader " << rank << " before created capio object" << std::endl;
    capio_mpi capio(true, false, rank, config_path);
    std::cout << "reader " << rank << " after created capio object" << std::endl;
    if (NUM_ELEM % size == 0) {
        int array_length = NUM_ELEM / size * num_prods;
        data = new int[array_length];
        expected_result = new int[array_length];
        std::cout << "before all gather process " << rank << std::endl;
        capio.capio_all_gather(nullptr, 0, data, array_length);
        std::cout << "after all gather process " << rank << std::endl;
        compute_expected_result_gather(expected_result, array_length, num_prods, 0);
        if (rank == 0) {
            std::cout << "received data" << std::endl;
            print_array(data, array_length, rank);
            std::cout << "expected result" << std::endl;
            print_array(expected_result, array_length, rank);
        }
        compare_expected_actual(data, expected_result, array_length);
        std::cout << "before all gather process " << rank << std::endl;
        capio.capio_all_gather(nullptr, 0, data, array_length);
        std::cout << "after all gather process " << rank << std::endl;
        compute_expected_result_gather(expected_result, array_length, num_prods, 1);
        if (rank == 0) {
            std::cout << "received data" << std::endl;
            print_array(data, array_length, rank);
            std::cout << "expected result" << std::endl;
            print_array(expected_result, array_length, rank);
        }
        compare_expected_actual(data, expected_result, array_length);
        free(data);
        free(expected_result);
    }
    std::cout << "reader " << rank << " ended " << std::endl;
    MPI_Finalize();
}