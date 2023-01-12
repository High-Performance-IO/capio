#include "../../src/circular_buffer.hpp"
#include "common.hpp"
#include <mpi.h>

void test_one_to_one(const std::string& buffer_name, long int max_num_elems, int* data, long int num_elems, long int num_writes, int rank) {
	Circular_buffer<int> c_buff(buffer_name + std::to_string(rank), max_num_elems, num_elems * sizeof(int));
	for (long int k = 0; k < num_writes; ++k) {
			c_buff.write(data);
	}
}

void initialize_data(int* data, size_t num_elems, size_t num_writes, int rank) {
		for (size_t i = 0; i < num_elems; ++i) {
			data[i] = i % 10 + rank;
		}
}

int main(int argc, char** argv) {
	int rank;
	size_t num_elems, num_writes, max_num_elems;
	MPI_Init(&argc, &argv);
	if (argc != 4) {
		std::cerr << "input error: 4 parameters must be passed" << std::endl;
		MPI_Finalize();
		return 1;
	}
	num_elems = std::atol(argv[1]);
	num_writes = std::atol(argv[2]);
	max_num_elems = std::atol(argv[3]);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	int* data = new int[num_elems];
	initialize_data(data, num_elems, num_writes, rank);
	std::cout << "max_num_elems size " << max_num_elems << std::endl;
	test_one_to_one("test_perf", max_num_elems, data, num_elems, num_writes, rank);
	delete[] data;
	MPI_Finalize();
	return 0;
}
