#include <iostream>
#include <mpi.h>
#include "../../capio_ordered/capio_ordered.hpp"



int main(int argc, char** argv) {
    int size, rank, *matrix;
    MPI_Init(&argc, &argv);
    if (argc != 4) {
        std::cout << "input error: num of rows, num of columns and config file needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    int num_rows = std::stoi(argv[1]);
    int num_cols = std::stoi(argv[2]);
    std::string config_path(argv[3]);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    capio_ordered capio(false, true, rank, config_path);
    std::cout << "writer " << rank << "created capio object" << std::endl;
    if (rank == 0) {
        matrix = new int[num_rows * num_cols];
        for (int i = 0; i < num_rows * num_cols; ++i)
            matrix[i] = i;
        capio.capio_scatter(matrix, nullptr, num_rows * num_cols / size);
        free(matrix);
    }
    std::cout << "writer " << rank << "ended " << std::endl;
    MPI_Finalize();
    return 0;
}