#include <iostream>
#include <mpi.h>
#include "../capio_ordered/capio_ordered.hpp"
#include "common/utils.hpp"

int main(int argc, char** argv) {
    int num;
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (argc != 3) {
        std::cout << "input error: capio buffer size and config file needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    int buf_size = std::stoi(argv[1]);
    std::string config_path = argv[2];
    capio_ordered capio(true, false, rank, buf_size, config_path);
    if (rank == size - 1) {
        std::cout << "reader " << rank << "created capio object" << std::endl;
        for (int i = 0; i < 100; ++i) {
            capio.capio_recv(&num, 1);
            std::cout << "received elem" << num << std::endl;
            compare_expected_actual(&num, &i, 1);
        }
    }
    std::cout << "reader " << rank << "ended " << std::endl;
    MPI_Finalize();
    return 0;
}
