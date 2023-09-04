#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>
#include <iostream>
#include <string>
#include "common.hpp"
#include "../../src/data_structure/circular_buffer.hpp"
#include <limits>
#include <chrono>
#include <fstream>
#include <mpi.h>


using cclock = std::chrono::system_clock;
using sec = std::chrono::duration<double>;

void test_one_to_one(const std::string& buffer_name, long int max_num_elems, char* data, long int num_bytes, long int num_reads, int rank) {
	Circular_buffer<char> c_buff(buffer_name + std::to_string(rank), max_num_elems, num_bytes); 

	cclock::time_point before;
	std::ofstream file;
	file.open("time_consumer.txt", std::fstream::app);
	// for milliseconds, use using ms = std::chrono::duration<double, std::milli>;
	before = cclock::now();
	for (long int i = 0; i < num_reads; ++i) {
		c_buff.read(data);
	}

	const sec duration = cclock::now() - before;
	file << "total consumer receive time: " << duration.count() << " secs" << std::endl;
	file.close();
	c_buff.free_shm();
}

void check(char* data, long int num_bytes) {
		for (long int i = 0; i < num_bytes; ++i) {
			if (data[i] != i % 10 + '0') {
				std::cerr << "error in receiving data" << std::endl;
				exit(1);
			}
		}
}

int main(int argc, char** argv) {
	int rank;
	long unsigned int num_bytes, num_reads, max_num_elems;
	MPI_Init(&argc, &argv);
	if (argc != 4) {
		std::cerr << "input error: 4 parameter must be passed" << std::endl;
		MPI_Finalize();
		return 1;
	}
	num_bytes = std::atol(argv[1]);
	num_reads = std::atol(argv[2]);
	max_num_elems = std::atol(argv[3]);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	char* data = new char[num_bytes];
	test_one_to_one("test_perf", max_num_elems, data, num_bytes, num_reads, rank);
	check(data, num_bytes);
	delete[] data;
	MPI_Finalize();
	return 0;
}
