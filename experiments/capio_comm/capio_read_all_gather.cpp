#include <iostream>
#include <mpi.h>
#include "../../capio_ordered/capio_ordered.hpp"


int main(int argc, char** argv) {
    int rank, size, *matrix;
    int num_rows, num_cols, buf_size;
    MPI_Init(&argc, &argv);
    if (argc != 5) {
        std::cout << "input error: capio buffer size, num of rows, num of columns and config file needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    buf_size = std::stoi(argv[1]);
    num_rows = std::stoi(argv[2]);
    num_cols = std::stoi(argv[3]);
    std::string config_path(argv[4]);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    matrix = new int [num_rows * num_cols * size];
    capio_ordered capio(true, false, rank, buf_size, config_path);
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