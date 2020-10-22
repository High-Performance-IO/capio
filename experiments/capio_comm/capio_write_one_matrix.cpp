#include "../../capio_ordered/capio_ordered.hpp"
#include "../commons/utils_exp.hpp"
#include <mpi.h>


int main(int argc, char** argv) {
    int rank, num_rows, num_cols, **matrix;
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
    capio_ordered capio(false, true, rank, config_path);
    if (rank == 0) {
        matrix = alloc_2d_int(num_rows, num_cols);
        for (int i = 0; i < num_rows; ++i) {
            for (int j = 0; j < num_cols; ++j) {
                matrix[i][j] = i + j + rank;
            }
        }
        std::cout << "before send" << std::endl;
        capio.capio_send(matrix[0], num_rows * num_cols, 0);
        std::cout << "after the send" << std::endl;
        free(matrix[0]);
        free(matrix);
    }
    std::cout << "producer terminates rank " << rank << std::endl;
    MPI_Finalize();
}
