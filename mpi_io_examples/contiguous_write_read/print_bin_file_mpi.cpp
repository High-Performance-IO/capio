#include <mpi.h>
#include <iostream>
int print_buf(int buf[], int size) {
    for (int i = 0; i < size; ++i) {
        std::cout << buf[i] << std::endl;
    }
}

bool get_input(int argc, char** argv, char** path, int &size) {
    int my_rank, comm_world_size;
    bool result = false; ;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_world_size);
    if (comm_world_size > 1) {
        if (my_rank == 0) {
            std::cout << "error: this program must be launched with only one process" << std::endl;
        }
    }
    else if (argc < 3) {
        std::cout << "input error: miss some inputs" << std::endl;
        std::cout << "inputs needed: path of the file to read and number of ints to read" << std::endl;

    }
    else {
        *path = argv[1];
        size = std::stoi(argv[2]);
        result = true;
    }
    return result;
}

// reads a binary file created using MPI I/O end prints the result on terminal

int main(int argc, char** argv) {
    int* buf;
    int size, open_result;
    MPI_Status status;
    MPI_File fh;
    char* path;
    MPI_Init(&argc, &argv);

    if (get_input(argc, argv, &path, size)) {
        buf = (int*) malloc(size * sizeof(int));
        open_result = MPI_File_open(MPI_COMM_WORLD, path, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
        if (open_result == MPI_SUCCESS) {
            MPI_File_read(fh, buf, size, MPI_INT, &status);
            print_buf(buf, size);
            MPI_File_close(&fh);
        }
        else {
            std::cout << "error path " + std::string(path) + " not valid " << std::endl;
        }
        free(buf);
    }

    MPI_Finalize();
}

