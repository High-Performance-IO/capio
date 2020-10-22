#include <fstream>
#include <iostream>
#include <mpi.h>
#include "../commons/utils_exp.hpp"

int main(int argc, char** argv) {
    int rank, **matrix;
    MPI_Init(&argc, &argv);
    if (argc != 3) {
        std::cout << "input error: num of rows and num of columns needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    int num_rows = std::stoi(argv[1]);
    int num_cols = std::stoi(argv[2]);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::ifstream file("output_file_" + std::to_string(rank) + ".txt");
    if (! file.is_open()) {
        std::cout << "error opening file" << std::endl;
        MPI_Finalize();
        return 1;
    }
    matrix = alloc_2d_int(num_rows, num_cols);
    for (int i = 0; i < num_rows; ++i) {
        for (int j = 0; j < num_cols; ++j) {
            file >> matrix[i][j];
        }
    }
    if (rank == 0)
        MPI_Reduce(MPI_IN_PLACE, matrix[0], num_rows * num_cols, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    else
        MPI_Reduce(matrix[0], nullptr, num_rows * num_cols, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        std::ofstream output_file("output_file_reduce.txt");
        for (int i = 0; i < num_rows; ++i) {
            for (int j = 0; j < num_cols; ++j) {
                output_file << matrix[i][j] << " ";
            }
            output_file << "\n";
        }
        output_file.close();
    }
    file.close();
    free(matrix[0]);
    free(matrix);
    MPI_Finalize();
    return 0;
}