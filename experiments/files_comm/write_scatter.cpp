#include <fstream>
#include <iostream>
#include <mpi.h>

int main(int argc, char** argv) {
    int rank, size, *matrix;
    int num_rows, num_cols;
    MPI_Init(&argc, &argv);
    if (argc != 3) {
        std::cout << "input error: num of rows and num of columns needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    num_rows = std::stoi(argv[1]);
    num_cols = std::stoi(argv[2]);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (rank == 0) {
        matrix = new int[num_rows * num_cols];
        for (int i = 0; i < num_rows * num_cols; ++i)
            matrix[i] = i;
        for (int k = 0; k < size; ++k) {
            std::ofstream output_file("output_file_" + std::to_string(k) + ".txt");
            for (int i = 0; i < num_rows * num_cols / size ; ++i) {
                output_file << matrix[i + k * (num_rows * num_cols / size)] << " ";
            }
            output_file.close();
        }
        free(matrix);
    }

    MPI_Finalize();
    return 0;
}