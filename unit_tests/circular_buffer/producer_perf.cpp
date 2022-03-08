#include "../../src/circular_buffer.hpp"
#include "common.hpp"
#include <mpi.h>

void test_one_to_one(const std::string& buffer_name, long int buff_size, int* data, long int num_elems, long int num_writes, int rank) {
	Circular_buffer<int> c_buff(buffer_name + std::to_string(rank), buff_size, num_elems * sizeof(int));
	for (long int k = 0; k < num_writes; ++k) {
			c_buff.write(data + k * num_elems);
	}
}

void initialize_data(int* data, long int num_elems, long int num_writes, int rank) {
	for (long int k = 0; k < num_writes; ++k) {
		for (long int i = 0; i < num_elems; ++i) {
			data[i + k * num_elems] = i % 10 + rank;
		}
	}
}

int main(int argc, char** argv) {
	int rank;
	long int num_elems, num_writes, buff_size;
	MPI_Init(&argc, &argv);
	if (argc != 4) {
		std::cerr << "input error: 4 parameters must be passed" << std::endl;
		MPI_Finalize();
		return 1;
	}
	num_elems = std::atol(argv[1]);
	num_writes = std::atol(argv[2]);
	buff_size = std::atol(argv[3]);
	if (buff_size < num_elems * sizeof(int)) {
		std::cerr << "input error: buff_size must be > num_elems * sizeof(int)" << std::endl;
		MPI_Finalize();
		return 1;
	}
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	int* data = new int[num_elems * num_writes];
	initialize_data(data, num_elems, num_writes, rank);
	std::cout << "buff size " << buff_size << std::endl;
	test_one_to_one("test_perf", buff_size, data, num_elems, num_writes, rank);
	delete[] data;
	MPI_Finalize();
	return 0;
}
