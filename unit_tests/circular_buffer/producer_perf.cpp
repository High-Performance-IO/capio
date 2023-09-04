#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>
#include <iostream>
#include <string>
#include "common.hpp"
#include <mpi.h>
#include "../../src/data_structure/circular_buffer.hpp"

void test_one_to_one(const std::string& buffer_name, long int max_num_elems, char* data, long int num_bytes, long int num_writes, int rank) {
	Circular_buffer<char> c_buff(buffer_name + std::to_string(rank), max_num_elems, num_bytes);
	for (long int k = 0; k < num_writes; ++k) {
			c_buff.write(data);
	}
}

void initialize_data(char* data, size_t num_bytes, int rank) {
		for (size_t i = 0; i < num_bytes; ++i) {
			data[i] = i % 10 + '0';
		}
}

int main(int argc, char** argv) {
	int rank;
	size_t num_bytes, num_writes, max_num_elems;
	MPI_Init(&argc, &argv);
	if (argc != 4) {
		std::cerr << "input error: 4 parameters must be passed" << std::endl;
		MPI_Finalize();
		return 1;
	}
	num_bytes = std::atol(argv[1]);
	num_writes = std::atol(argv[2]);
	max_num_elems = std::atol(argv[3]);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	char* data = new char[num_bytes];
	initialize_data(data, num_bytes, rank);
	test_one_to_one("test_perf", max_num_elems, data, num_bytes, num_writes, rank);
	delete[] data;
	MPI_Finalize();
	return 0;
}
