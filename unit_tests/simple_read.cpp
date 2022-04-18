#include <iostream>
#include <fstream>
#include <chrono>
#include <limits>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>

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

using cclock = std::chrono::system_clock;
using sec = std::chrono::duration<double>;

void read_from_file(const std::string file_name, int* data, long int num_elements, long int num_reads, int rank, std::string time_file) {
	int fd = open(file_name.c_str(), O_RDONLY, 0644);
	if (fd < 0) {
		std::cerr << "reader " << rank << " failed to open the file: " << file_name << std::endl;
		MPI_Finalize();
		exit(1);
	}
	std::ofstream file;
	cclock::time_point before;
	if (rank == 0) {
		file.open(time_file, std::fstream::app);
		// for milliseconds, use using ms = std::chrono::duration<double, std::milli>;
		before = cclock::now();
	}
	for (long int i = 0; i < num_reads; ++i) {
		long int k = 0;
    	while (k < num_elements) {
        	long int read_res = read(fd, data + i * num_elements + k, sizeof(int) * (num_elements - k));
        	long int num_elements_read = (read_res / sizeof(int));
        	k += num_elements_read;
        	if (read_res < 0) {
            	std::cerr << "reader " << rank << " failed to read the file" << std::endl;
            	delete[] data;
				MPI_Finalize();
				exit(1);
        	}
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0) {
		const sec duration = cclock::now() - before;
		file << "total write time: " << duration.count() << " secs" << std::endl;
		file.close();
	}

	if (close(fd) == -1)
		std::cerr << "process " << rank << ", error closing the file\n";
}

int main (int argc, char** argv) {
	int rank;
	long int num_elements, num_reads;
	std::string time_file;
	MPI_Init(&argc, &argv);
	if (argc != 4) {
		std::cerr << "simple read input error" << std::endl;
		MPI_Finalize();
		return 0;
	}
	num_elements = std::atol(argv[1]);
	num_reads = std::atol(argv[2]);
	time_file = argv[3];
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	std::string file_name = "file_" + std::to_string(rank) + ".txt";
	int* data = new int[num_elements * num_reads];
	read_from_file(file_name, data, num_elements, num_reads, rank, time_file);
	int sum = sum_all(data, num_elements, num_reads);
	std::cout << "process " << rank << ", sum = " << sum << "\n";
	delete[] data;
	MPI_Finalize();
}

