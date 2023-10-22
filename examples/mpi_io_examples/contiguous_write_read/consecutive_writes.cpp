#include <mpi.h>
#include <iostream>
void initialize_buffer(int buf[], int size) {
    for (int i = 0; i < size; ++i) {
        buf[i] = i;
        std::cout << "buf[i]: " << buf[i] << std::endl;
    }
}

// all processes write in the same file a sequence of numbers 0, 1, ..., 10

int main(int argc, char** argv) {
    int my_rank, result_open;
    MPI_File fh;
    int buf[10];
    int size = 10;
    MPI_Init( &argc, &argv );
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    initialize_buffer(buf, size);
    result_open =  MPI_File_open(MPI_COMM_WORLD, "./output_bin", MPI_MODE_CREATE | MPI_MODE_RDWR, MPI_INFO_NULL, &fh);
    if (result_open == MPI_SUCCESS) {
        MPI_File_set_view(fh, my_rank * size * sizeof(int), MPI_INT, MPI_INT, "native", MPI_INFO_NULL);
        MPI_File_write(fh, buf, size, MPI_INT, MPI_STATUS_IGNORE);
        MPI_File_close(&fh);
    }
    else {
        std::cout << "error: impossible open file ./output_bin" << std::endl;
    }
    MPI_Finalize();
}


