#include <fstream>
#include <iostream>
#include <mpi.h>
#include "../commons/utils_exp.hpp"

int main(int argc, char** argv) {
    int size, rank, **matrix;
    int num_rows, num_cols;
    MPI_Init(&argc, &argv);
    if (argc != 3) {
        std::cout << "input error: num of rows and num of columns needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    num_rows = std::stoi(argv[1]);
    num_cols = std::stoi(argv[2]);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (rank == 0) {
        matrix = alloc_2d_int(num_rows, num_cols);
        for (int i = 0; i < num_rows; ++i)
            for (int j = 0; j < num_cols; ++j)
                matrix[i][j] = i + j;
        for (int k = 0; k < size; ++k) {
            std::ofstream output_file("output_file_" + std::to_string(k) + ".txt");
            for (int i = 0; i < num_rows ; ++i) {
                for (int j = 0; j < num_cols; ++j)
                    output_file << matrix[i][j] << " ";
            }
            output_file.close();
        }
        free(matrix[0]);
        free(matrix);
    }

    MPI_Finalize();
    return 0;
}