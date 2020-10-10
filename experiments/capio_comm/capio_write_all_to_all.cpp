#include "../../capio_mpi/capio_mpi.hpp"
#include <mpi.h>

int **alloc_2d_int(int rows, int cols) {
    int *data = (int *)malloc(rows*cols*sizeof(int));
    int **array= (int **)malloc(rows*sizeof(int*));
    for (int i=0; i<rows; i++)
        array[i] = &(data[cols*i]);

    return array;
}


int main(int argc, char** argv) {
    int size, rank, num_rows, num_cols, **matrix;
    MPI_Init(&argc, &argv);
    if (argc != 4) {
        std::cout << "input error: num of rows, num of columns and config file needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    num_rows = std::stoi(argv[1]);
    num_cols = std::stoi(argv[2]);
    std::string config_path(argv[3]);
    matrix = alloc_2d_int(num_rows, num_cols);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    capio_mpi capio(false, true, rank, config_path);
    for (int i = 0; i < num_rows; ++i) {
        for (int j = 0; j < num_cols; ++j) {
            matrix[i][j] = i + j + rank;
        }
    }
    capio.capio_all_to_all(matrix[0], num_rows * num_cols / size, nullptr);
    free(matrix[0]);
    free(matrix);
    MPI_Finalize();
}