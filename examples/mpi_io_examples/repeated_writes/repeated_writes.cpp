#include <algorithm>
#include <iostream>
#include <mpi.h>

/*
 *  initialize the buffer  using the rank of the process
 *
 *  parameters
 *  int buf[]: buffer to initialize
 *  int size: size of the buffer
 *  int rank: rank of the process
 *
 *  returns NONE
 */

void initialize_buffer(int buf[], int size, int rank) {
    for (int i = 0; i < size; ++i) {
        buf[i] = i + rank;
    }
}

/*
 * each process write its buffer in order of rank
 * This is repeated for six times
 */

int main(int argc, char **argv) {
    int rank, result_open, comm_size;
    MPI_File fh;
    int buf[10];
    int size = 10;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    initialize_buffer(buf, size, rank);
    result_open = MPI_File_open(MPI_COMM_WORLD, "./output_bin", MPI_MODE_CREATE | MPI_MODE_RDWR,
                                MPI_INFO_NULL, &fh);
    if (result_open == MPI_SUCCESS) {
        MPI_File_set_view(fh, rank * size * sizeof(int), MPI_INT, MPI_INT, "native", MPI_INFO_NULL);
        MPI_File_write(fh, buf, size, MPI_INT, MPI_STATUS_IGNORE);
        for (int j = 1; j <= 5; ++j) {
            std::for_each(buf, buf + size, [](int &n) { ++n; });
            MPI_File_set_view(fh, rank * size * sizeof(int) + j * size * sizeof(int) * comm_size,
                              MPI_INT, MPI_INT, "native", MPI_INFO_NULL);
            MPI_File_write(fh, buf, size, MPI_INT, MPI_STATUS_IGNORE);
        }
        MPI_File_close(&fh);
    } else {
        std::cout << "error: impossible open file ./output_bin" << std::endl;
    }
    MPI_Finalize();
}