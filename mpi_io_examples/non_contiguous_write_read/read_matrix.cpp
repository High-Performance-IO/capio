#include <mpi.h>
#include <fstream>
#include <cmath>

// each worker write its block in a different local file

void print_partial_matrix(int array[], int blocks_side_length, int my_rank) {
    std::ofstream my_file;
    my_file.open ("block_" + std::to_string(my_rank) + ".txt");
    for (int i = 0; i < blocks_side_length; ++i) {
        for (int j = 0; j < (blocks_side_length - 1); ++j) {
            my_file << array[i * blocks_side_length + j] << " ";
        }
        //last num of the row doesn't precede a space
        my_file << array[i * blocks_side_length + (blocks_side_length - 1)];
        my_file << "\n";  //to avoid automatic flush of the buffer
    }
    my_file.close();
    return;
}

// write global matrix into a file. The matrix is stored in buf in row-major order

void write_matrix(int buf[], int blocks_size, int num_processes, int blocks_side_length) {
    std::ofstream matrix_file;
    int num_blocks_per_row;
    int num_block_per_cols;
    int rank_proc;
    num_block_per_cols = num_blocks_per_row = std::sqrt(num_processes);
    matrix_file.open ("output_matrix.txt");
    for (int i = 0; i < num_blocks_per_row; ++i) {
        for (int j = 0; j < blocks_side_length; ++j) {
            for (int k = 0; k < num_block_per_cols; ++k) {
                for (int z = 0; z < blocks_side_length; ++z) {
                    rank_proc = i * num_blocks_per_row + k;
                    matrix_file << buf[rank_proc * blocks_size + (j * blocks_side_length + z)] << " ";
                }
            }
            matrix_file << "\n";
        }
    }
    matrix_file.close();
}

// master : process with rank equals to 0

void master(int num_elems, int blocks_side_length, int num_processes) {
    int buf[num_elems];
    MPI_Status status;
    int blocks_size;
    blocks_size = blocks_side_length * blocks_side_length;
    for (int rank = 1; rank < num_processes; ++rank) {
        std::cout << "master start receive from " << rank << std::endl;
        MPI_Recv(buf + ((rank - 1) * blocks_size), blocks_size , MPI_INT, rank, 0, MPI_COMM_WORLD, &status);
        std::cout << "master received data from " << rank << std::endl;
    }
    write_matrix(buf, blocks_size, num_processes, blocks_side_length);
        std::cout << "master ended computation " << std::endl;
}

// Create a datatype representing a distributed array using specific parameters for the problem

void create_darray(int my_rank, int num_workers, MPI_Datatype* p_filetype, int matrix_side_length) {
    int array_of_gsizes[2];
    int array_of_distribs[2];
    int array_of_dargs[2];
    int array_of_psizes[2];
    array_of_gsizes[0] = array_of_gsizes[1] = matrix_side_length;
    array_of_distribs[0] = array_of_distribs[1] = MPI_DISTRIBUTE_BLOCK;
    array_of_dargs[0] = array_of_dargs[1] = MPI_DISTRIBUTE_DFLT_DARG;
    array_of_psizes[0] = array_of_psizes[1] = std::sqrt(num_workers);
    MPI_Type_create_darray(num_workers, my_rank, 2, array_of_gsizes, array_of_distribs,
                           array_of_dargs, array_of_psizes, MPI_ORDER_C,
                           MPI_FLOAT, p_filetype);
}

/*
 * reads the block from the global matrix. The block is put in the buffer local_array
 */

void read_block(int worker_rank, MPI_Comm workers_comm, int local_array[], int local_array_size, int matrix_side_length) {
    MPI_File fh;
    MPI_Status status;
    MPI_Datatype filetype;
    int num_workers, open_result;
    MPI_Comm_size(workers_comm, &num_workers);
    create_darray(worker_rank, num_workers, &filetype, matrix_side_length);
    MPI_Type_commit(&filetype);
    open_result = MPI_File_open(workers_comm, "./matrix_bin", MPI_MODE_CREATE | MPI_MODE_RDWR,
                  MPI_INFO_NULL, &fh);
    if (open_result == MPI_SUCCESS) {
        MPI_File_set_view(fh, 0, MPI_FLOAT, filetype, "native", MPI_INFO_NULL);
        std::cout << "process " << worker_rank << " is reading " << std::endl;
        MPI_File_read_all(fh, local_array, local_array_size, MPI_FLOAT, &status);
        MPI_File_close(&fh);
    }
    else {
        std::cout << "error: impossible open file ./matrix_bin" << std::endl;
    }
}

// worker : process with rank > 0

void worker(int blocks_side_length, int matrix_side_length, MPI_Comm workers_comm) {
    int local_array[blocks_side_length * blocks_side_length];
    int local_array_size;
    int worker_rank;
    int mpi_error;
    MPI_Comm_rank(workers_comm, &worker_rank);
    local_array_size = blocks_side_length * blocks_side_length;
    read_block(worker_rank, workers_comm, local_array, local_array_size, matrix_side_length);
    std::cout << "worker " << worker_rank << " is writing its block " << std::endl;
    print_partial_matrix(local_array, blocks_side_length, worker_rank);
    std::cout << "worker " << worker_rank << " ended to write " << std::endl;
    std::cout << "local_array_size " << local_array_size << std::endl;
    mpi_error = MPI_Send(local_array, local_array_size, MPI_INT, 0, 0,
                  MPI_COMM_WORLD);
    if (mpi_error != MPI_SUCCESS) {
        std::cout << "worker " << worker_rank << " error during send" << std::endl;
    }
    std::cout << "worker " << worker_rank << " ended its computation " << std::endl;
}

// function used by the master to print error messages

void print_error(int my_rank, int error_code, std::string additional_msg = "") {
    if (my_rank == 0) {
        switch (error_code) {
            case 0:
                std::cout << "input error: side length needed" << std::endl;
                break;
            case 1:
                std::cout << "input error: the number of worker must be a square number" << std::endl;
                break;
            case 2:
                std::cout << "input error: the matrix can't be divided in square blocks using the numbers"
                             " of processes given" << std::endl;
                break;
            default:
                std::cout << "undefined error" << std::endl;
        }
        if (! additional_msg.empty()) {
            std::cout << additional_msg << std::endl;
        }
    }
}

// returns true if the input is a square number, false otherwise

bool is_square_number(long double x) {
    long double sr = sqrt(x);

    return ((sr - floor(sr)) == 0);
}


// gets the command line inputs
// returns true if the inputs are corrects and complete, false otherwise

bool get_inputs(int my_rank, int argc, char** argv, int& matrix_side_length,
        int num_processes) {
    bool result = true;
    int num_workers = num_processes - 1;
    if (argc != 2) {
        print_error(my_rank, 0);
        result = false;
    }
    else if (! is_square_number(num_workers)) {
        print_error(my_rank, 1, "num of workers given: " + std::to_string(num_workers));
        result = false;
    }
    else  {
        matrix_side_length = std::stoi(argv[1]);
        if (matrix_side_length % ((int) std::sqrt(num_workers)) != 0) {
            print_error(my_rank, 2,
                    "num of processes given: " + std::to_string(num_processes));
            result = false;
        }

    }
    return result;
}

// it the command line inputs are corrects and complete then starts computation for the master and the workers

void start(int argc, char** argv, MPI_Comm workers_comm) {
    int my_rank, num_processes;
    int num_elems, matrix_side_length;
    int blocks_side_length;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_processes);
    if (get_inputs(my_rank, argc, argv, matrix_side_length, num_processes)) {
        num_elems = matrix_side_length * matrix_side_length;
        blocks_side_length =  matrix_side_length / std::sqrt(num_processes - 1);
        if (my_rank == 0) {
            master(num_elems, blocks_side_length, num_processes);
        }
        else {
            worker(blocks_side_length, matrix_side_length, workers_comm);
        }
    }
}

/*
 * precondition: the input matrix is a square matrix
 *
 * the workers processes read a block from a square matrix stored into a binary file created using MPI I/O
 * then each worker sends its block to a master process (with rank equals to 0).
 * after received all the blocks, the master print the matrix in a text file
 */

int main(int argc, char** argv) {
    int ranks[1] = {0};
    MPI_Group world_group;
    MPI_Group workers_group;
    MPI_Comm workers_comm;

    MPI_Init(&argc, &argv);
    MPI_Comm_group(MPI_COMM_WORLD, &world_group);
    MPI_Group_excl(world_group, 1, ranks, &workers_group);
    // Create a new communicator based on the group
    MPI_Comm_create_group(MPI_COMM_WORLD, workers_group, 0, &workers_comm);

    start(argc, argv, workers_comm);

    // free resources
    MPI_Group_free(&world_group);
    MPI_Group_free(&workers_group);
    if (workers_comm != MPI_COMM_NULL) { // the master can't free the workers_comm
        MPI_Comm_free(&workers_comm);
    }

    MPI_Finalize();
}