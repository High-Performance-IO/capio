#include <iostream>
#include <mpi.h>
#include "../../../capio_ordered/capio_ordered.hpp"
#include "../../common/utils.hpp"

int const NUM_ELEM = 100;

int main(int argc, char** argv) {
    int* data;
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (argc != 4) {
        std::cout << "input error: capio buffer size, number of consumers and config file needed " << std::endl;
        MPI_Finalize();
        return 0;
    }
    int buf_size = std::stoi(argv[1]);
    int num_cons = std::stoi(argv[2]);
    std::string config_path = argv[3];
    std::cout << "writer " << rank << "before created capio object" << std::endl;
    capio_ordered capio(false, true, rank, buf_size, config_path);
    std::cout << "writer " << rank << "created capio object" << std::endl;
    if (NUM_ELEM % size == 0) {
        int array_length = NUM_ELEM / num_cons;
        data = new int[array_length];
        initialize(data, array_length, rank);
        std::cout << "before all gather process " << rank << std::endl;
        capio.capio_all_gather(data, array_length, nullptr, -1);
        std::cout << "after all gather process " << rank << std::endl;
        initialize(data, array_length, rank + 1);
        std::cout << "before all gather process " << rank << std::endl;
        capio.capio_all_gather(data, array_length, nullptr, -1);
        std::cout << "after all gather process " << rank << std::endl;
        free(data);
    }
    std::cout << "writer " << rank << "ended " << std::endl;
    MPI_Finalize();
    return 0;
}