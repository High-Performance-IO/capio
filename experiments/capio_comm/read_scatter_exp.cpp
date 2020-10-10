#include <iostream>
#include <mpi.h>
#include "../../capio_mpi/capio_mpi.hpp"




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
    capio_mpi capio(true, false, rank, config_path);
    matrix = new int[num_rows * num_cols];
    capio.capio_scatter(nullptr, matrix, num_rows * num_cols / size);
    std::ofstream output_file("output_file_read_scatter_capio_" + std::to_string(rank) + ".txt");
    for (int i = 0; i < num_rows * num_cols / size; ++i) {
        output_file << matrix[i] << "\n";
    }
    output_file.close();
    free(matrix);
    MPI_Finalize();
}