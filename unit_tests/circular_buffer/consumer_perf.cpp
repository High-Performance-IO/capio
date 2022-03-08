#include "../../src/circular_buffer.hpp"
#include "common.hpp"
#include <limits>
#include <mpi.h>

void test_one_to_one(const std::string& buffer_name, long int buff_size, int* data, long int num_elems, long int num_reads, int rank) {
	Circular_buffer<int> c_buff(buffer_name + std::to_string(rank), buff_size, num_elems * sizeof(int));
	for (long int i = 0; i < num_reads; ++i) {
		c_buff.read(data + i * num_elems);
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

int main(int argc, char** argv) {
	int rank;
	long int num_elems, num_reads, buff_size;
	MPI_Init(&argc, &argv);
	if (argc != 4) {
		std::cerr << "input error: 4 parameter must be passed" << std::endl;
		MPI_Finalize();
		return 1;
	}
	num_elems = std::atol(argv[1]);
	num_reads = std::atol(argv[2]);
	buff_size = std::atol(argv[3]);
	if (buff_size < num_elems * sizeof(int)) {
		std::cerr << "input error: buff_size must be >= num_elems * sizeof(int)" << std::endl;
		return 1;
	}
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	int* data = new int[num_elems * num_reads];
	test_one_to_one("test_perf", buff_size, data, num_elems, num_reads, rank);
	int sum = sum_all(data, num_elems, num_reads);
	std::cout << "process " << rank << ", sum = " << sum << "\n";
	delete[] data;
	MPI_Finalize();
	return 0;
}
