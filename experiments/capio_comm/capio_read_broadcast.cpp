#include "../../capio_ordered/capio_ordered.hpp"
#include <mpi.h>
#include "../commons/utils_exp.hpp"


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
    matrix = alloc_2d_int(num_rows, num_cols);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    capio_ordered capio(true, false, rank, config_path);
    capio.capio_broadcast(matrix[0], num_rows * num_cols, 0);
    std::ofstream output_file("output_file_capio_broadcast_" + std::to_string(rank) + ".txt");
    for (int i = 0; i < num_rows; ++i) {
        for(int j = 0; j < num_cols; ++j) {
            output_file << matrix[i][j] << "\n";
        }
    }
    output_file.close();
    free(matrix[0]);
    free(matrix);
    MPI_Finalize();
}