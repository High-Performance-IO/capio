#include <iostream>
#include <mpi.h>
#include "../../capio_mpi/capio_mpi.hpp"


int main(int argc, char** argv) {
    int rank, size, *matrix;
    int num_rows, num_cols;
    MPI_Init(&argc, &argv);
    if (argc != 4) {
        std::cout << "input error: num of rows, num of columns and config file needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    num_rows = std::stoi(argv[1]);
    num_cols = std::stoi(argv[2]);
    std::string config_path(argv[3]);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    matrix = new int [num_rows * num_cols * size];
    capio_mpi capio(true, false, rank, config_path);
    capio.capio_all_gather(nullptr, 0,matrix, num_rows * num_cols * size);
    std::ofstream output_file("output_capio_all_gather_" + std::to_string(rank) + ".txt");
    for (int i = 0; i < num_rows *size; ++i) {
        for (int j = 0; j < num_cols; ++j) {
            output_file << matrix[i * num_cols + j] << " ";
        }
        output_file << "\n"; // vs std::endl
    }
    output_file.close();
    free(matrix);
    MPI_Finalize();
    return 0;
}