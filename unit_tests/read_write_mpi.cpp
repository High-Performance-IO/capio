#include <iostream>
#include <mpi.h>
#include <limits>

void writer(int rank, int* array, long int num_writes, long int num_elements, int receiver) {
    for (long int i = 0; i < num_writes; ++ i) {
		long int num_elements_to_send = 0;
		for (long int k = 0; k < num_elements; k += num_elements_to_send) {
			if (num_elements - num_elements_to_send > 1024L * 1024 * 1024 / sizeof(int))
				num_elements_to_send = 1024L * 1024 * 1024 / sizeof(int);
			else
				num_elements_to_send = num_elements - num_elements_to_send;
        	MPI_Send(array + num_elements * i + k, num_elements_to_send, MPI_INT, receiver, 0, MPI_COMM_WORLD);
		}
    }
}

int sum_all(int *data, long int num_elements, long int num_reads) {
	int sum = 0;
	for (long int k = 0; k < num_reads; ++k) {
		for (long int i = 0; i < num_elements; ++i) {
        	if (sum > std::numeric_limits<int>::max() - data[i + k * num_elements]) {
            	sum = 0;
        	}
				sum += data[i + k * num_elements];
		}
	}
	return sum;
}

void reader(int rank, int* array, long int num_reads, long int num_elements, int sender) {
	MPI_Status status;
    for (long int i = 0; i < num_reads; ++i) {
		int num_elements_received = 0;
		for (long int k = 0; k < num_elements; k += num_elements_received) {
        	MPI_Recv(array + num_elements * i + k, num_elements - k, MPI_INT, sender, 0, MPI_COMM_WORLD, &status);
			MPI_Get_count(&status, MPI_INT, &num_elements_received);
		}
    }
	int sum = sum_all(array, num_elements, num_reads);
    std::cout << "reader " << rank << " sum: " << sum << std::endl;
}

void initialize_data(int* data, long int num_elements, long int num_writes, int rank) {
	for (long int k = 0; k < num_writes; ++k) {
		for (long int i = 0; i < num_elements; ++i) {
			data[i + k * num_elements] = i % 10 + rank;
		}
	}
}

int main(int argc, char** argv) {
    int rank, size;
	long int num_elements, num_io_ops;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (argc != 3) {
        std::cerr << "input error: number of elements and number of I/O operations needed" << std::endl;
        MPI_Finalize();
        return 0;
    }
    num_elements = std::atol(argv[1]);
    num_io_ops = std::atol(argv[2]);
	int* data = new int[num_elements * num_io_ops];
    if (rank < size / 2) {
		initialize_data(data, num_elements, num_io_ops, rank);
        writer(rank, data, num_io_ops, num_elements, size / 2 + rank);
    }
    else {
        reader(rank, data, num_io_ops, num_elements, rank - size / 2 );
    }
    delete[] data;
    MPI_Finalize();
}
