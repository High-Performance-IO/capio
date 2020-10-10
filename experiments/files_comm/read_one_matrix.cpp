#include <fstream>
#include <iostream>
#include <mpi.h>


void print_matrix(int** matrix, int num_rows, int num_cols) {
    for (int i = 0; i < num_rows; ++i) {
        for (int j = 0; j < num_cols; ++j) {
            std::cout << matrix[i][j] << " " << std::endl;
        }
        std::cout << std::endl;
    }
}

int main(int argc, char** argv) {
    int rank, size, *matrix;
    MPI_Init(&argc, &argv);
    if (argc != 3) {
        std::cout << "input error: num of rows and num of columns needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    int num_rows = std::stoi(argv[1]);
    int num_cols = std::stoi(argv[2]);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (rank == 0) {
        std::ifstream file("output_file_" + std::to_string(rank) + ".txt");
        if (! file.is_open()) {
            std::cout << "error opening file" << std::endl;
            MPI_Finalize();
            return 1;
        }
        matrix = new int[num_rows * num_cols];

        for (int i = 0; i < num_rows * num_cols; ++i) {
            file >> matrix[i];
        }
        std::ofstream output_file("output_file_read_one_to_one.txt");
        for (int i = 0; i < num_rows ; ++i) {
            for (int j = 0; j < num_cols ; ++j) {
                output_file << matrix[i * num_cols + j] << " ";
            }
            output_file << "\n"; // vs std::endl
        }
        output_file.close();

        file.close();
        free(matrix);
    }
    MPI_Finalize();
    return 0;
}