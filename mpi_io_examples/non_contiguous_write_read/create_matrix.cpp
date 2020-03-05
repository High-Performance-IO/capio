#include <mpi.h>
#include <cmath>

/*
 * initialize the local matrix
 * each element is equals to the sum of its row and column
 */

void initialize_local_array(int side_length_block, int array[]) {
    for (int i = 0; i < side_length_block; ++i) {
        for (int j = 0; j < side_length_block; ++j) {
            array[i * side_length_block + j] = i + j;
        }
    }
}

// function used by the process with rank 0 to print error messages

void print_error_msg(int rank, int error_code) {
    if (rank != 0) { // only one process print the message error
        return;
    }
    switch (error_code) {
        case 0:
            std::cout << "input error: the side length of the matrix is needed in input" << std::endl;
            break;
        case 1:
            std::cout << "input error: the side length of the matrix must be even and > 0" << std::endl;
            break;
        case 2:
            std::cout << "input error: the number of processes must a square number" << std::endl;
            break;
        case 3:
            std::cout << "input error: the matrix can't be divided in square blocks using the numbers"
                         " of processes given" << std::endl;
            break;
        default :
            std::cout << "undefined error" << std::endl;
    }
}

// returns true if the input is a square number, false otherwise

bool is_square_number(long double x) {
    long double sr = sqrt(x);

    return ((sr - floor(sr)) == 0);
}

// gets the command line inputs
// returns true if the inputs are corrects and complete, false otherwise

bool get_input(int my_rank, int num_processes, int argc, char** argv, int& side_length_matrix) {
    bool result = true;
    if (argc != 2) {
        print_error_msg(my_rank, 0);
        result = false;
    }
    else {
        side_length_matrix = std::stoi(argv[1]);
        if (side_length_matrix % 2 != 0 || side_length_matrix == 0) {
            print_error_msg(my_rank, 1);
            result = false;
        }
        else if (! is_square_number(num_processes)) {
            print_error_msg(my_rank, 2);
            result = false;
        }
        else if (side_length_matrix %  ((int) std::sqrt(num_processes)) != 0) {
            print_error_msg(my_rank, 3);
            result = false;
        }
    }
    return result;
}

// the local matrices are written in the same file to create a bigger square matrix

void write_matrix(int my_rank, int num_processes, int side_length_matrix, int * local_array,
        int local_array_size) {
    MPI_File fh;
    MPI_Status status;
    MPI_Datatype filetype;
    int array_of_gsizes[2];
    int array_of_distribs[2];
    int array_of_dargs[2];
    int array_of_psizes[2];
    int open_result;
    array_of_gsizes[0] =  array_of_gsizes[1] = side_length_matrix;
    array_of_distribs[0] = array_of_distribs[1] = MPI_DISTRIBUTE_BLOCK;
    array_of_dargs[0] = array_of_dargs[1] = MPI_DISTRIBUTE_DFLT_DARG;
    array_of_psizes[0] = array_of_psizes[1] = std::sqrt(num_processes);
    MPI_Type_create_darray(num_processes, my_rank, 2, array_of_gsizes, array_of_distribs,
                           array_of_dargs, array_of_psizes, MPI_ORDER_C,
                           MPI_FLOAT, &filetype);
    MPI_Type_commit(&filetype);
    open_result = MPI_File_open(MPI_COMM_WORLD, "./matrix_bin", MPI_MODE_CREATE | MPI_MODE_RDWR,
                  MPI_INFO_NULL, &fh);
    if (open_result == MPI_SUCCESS) {
        MPI_File_set_view(fh, 0, MPI_FLOAT, filetype, "native", MPI_INFO_NULL);
        MPI_File_write_all(fh, local_array, local_array_size, MPI_FLOAT, &status);
        MPI_File_close(&fh);
    }
    else {
        std::cout << "error: impossible open file ./matrix_bin" << std::endl;
    }
}

/*
 * every process creates a local square matrix.
 * all the local matrices are written in the same file to create a bigger square matrix
 * each local matrix is written in order considering the rank.
 * example 4 process
 * process 0 -> block 0 | process 1 -> block 1 | process 2 -> block 2 | process 3 -> block 3
 *
 * final square matrix:
 *  _________________
 * |block 0 | block 1|
 * |block 2 | block 3|
 */

int main(int argc, char** argv) {
    int my_rank, local_array_size, num_processes;
    int side_length_matrix, side_length_block;
    int* local_array;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_processes);
    if (get_input(my_rank, num_processes, argc, argv, side_length_matrix)) {
        side_length_block = side_length_matrix / std::sqrt(num_processes);
        local_array_size = side_length_block * side_length_block;
        std::cout << "before initialization local array " << std::endl;
        local_array = (int*) malloc(sizeof(int) * local_array_size);
        initialize_local_array(side_length_block, local_array);
        std::cout << "after initialization local array " << std::endl;
        write_matrix(my_rank, num_processes, side_length_matrix, local_array, local_array_size);
        free(local_array);
    }


    MPI_Finalize();
}