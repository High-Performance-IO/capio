#include <iostream>
#include <mpi.h>
void print_buf(char buf[], int size) {
    std::cout << "print buffer" << std::endl;
    for (int i = 0; i < size; ++i) {
        std::cout << buf[i] << std::endl;
    }
}

bool get_input(int argc, char **argv, char **path) {
    int my_rank, comm_world_size;
    bool result = false;
    ;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_world_size);
    if (comm_world_size > 1) {
        if (my_rank == 0) {
            std::cout << "error: this program must be launched with only one process" << std::endl;
        }
    } else if (argc != 2) {
        std::cout << "input error: miss some inputs" << std::endl;
        std::cout << "inputs needed: path of the file to read" << std::endl;

    } else {
        *path = argv[1];
        result = true;
    }
    return result;
}

// reads a binary file created using MPI I/O end prints the result on terminal

int main(int argc, char **argv) {
    char buf[1024];
    int open_result, num_read;
    MPI_Status status;
    MPI_File fh;
    char *path;
    MPI_Init(&argc, &argv);
    num_read = 1024;
    for (int i = 0; i < 1024; ++i) {
        buf[i] = 'a';
    }
    if (get_input(argc, argv, &path)) {
        open_result = MPI_File_open(MPI_COMM_WORLD, path, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
        if (open_result == MPI_SUCCESS) {
            while (num_read == 1024) {
                MPI_File_read(fh, buf, 1024, MPI_CHAR, &status);
                MPI_Get_count(&status, MPI_CHAR, &num_read);
                print_buf(buf, num_read);
            }
            std::cout << "num_read " << num_read << std::endl;
            MPI_File_close(&fh);
        } else {
            std::cout << "error path " + std::string(path) + " not valid " << std::endl;
        }
    }

    MPI_Finalize();
}
