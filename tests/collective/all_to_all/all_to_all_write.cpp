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
    capio_ordered capio(false, true, rank, buf_size, config_path);
    std::cout << "writer " << rank << "created capio object" << std::endl;
    data = new int[NUM_ELEM];
    initialize(data, NUM_ELEM, rank);
    capio.capio_all_to_all(data, NUM_ELEM / num_cons, nullptr);
    capio.capio_all_to_all(data, NUM_ELEM / num_cons, nullptr);
    free(data);
    std::cout << "writer " << rank << "ended " << std::endl;
    MPI_Finalize();
    return 0;
}