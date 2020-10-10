#include <fstream>
#include <iostream>
#include <mpi.h>


int **alloc_2d_int(int rows, int cols) {
    int *data = (int *)malloc(rows*cols*sizeof(int));
    int **array= (int **)malloc(rows*sizeof(int*));
    for (int i=0; i<rows; i++)
        array[i] = &(data[cols*i]);

    return array;
}

int main(int argc, char** argv) {
    int rank, **matrix;
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
    if (rank == 0) {
        matrix = alloc_2d_int(num_rows, num_cols);
        for (int i = 0; i < num_rows; ++i) {
            for (int j = 0; j < num_cols; ++j) {
                matrix[i][j] = i + j + rank;
            }
        }
        std::ofstream output_file("output_file_" + std::to_string(rank) + ".txt");
        for (int i = 0; i < num_rows; ++i) {
            for (int j = 0; j < num_cols; ++j) {
                output_file << matrix[i][j] << " ";
            }
            output_file << "\n"; // vs std::endl
        }
        output_file.close();
        free(matrix[0]);
        free(matrix);
    }
    MPI_Finalize();
    return 0;
}