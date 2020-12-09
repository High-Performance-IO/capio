#include <iostream>
#include <mpi.h>
#include "../../capio_ordered/capio_ordered.hpp"

int main(int argc, char** argv) {
    int size, rank, *matrix;
    MPI_Init(&argc, &argv);
    if (argc != 5) {
        std::cout << "input error: capio buffer size, num of rows, num of columns and config file needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    int buf_size = std::stoi(argv[1]);
    int num_rows = std::stoi(argv[2]);
    int num_cols = std::stoi(argv[3]);
    std::string config_path(argv[4]);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    capio_ordered capio(true, false, rank, buf_size, config_path);
    if (rank == 0) {
        matrix = new int[num_rows * num_cols];
        std::cout << "consumer before read" << std::endl;
        capio.capio_recv(matrix, num_rows * num_cols);
        std::cout << "consumer after read" << std::endl;
        std::ofstream output_file("output_file_capio_read_one.txt");
        for (int i = 0; i < num_rows ; ++i) {
            for (int j = 0; j < num_cols; ++j) {
                output_file << matrix[i * num_cols + j] << " ";
            }
            output_file << "\n";
        }
        output_file.close();
        free(matrix);
    }
    std::cout << "consumer " << rank << " terminates" << std::endl;
    MPI_Finalize();
    return 0;
}