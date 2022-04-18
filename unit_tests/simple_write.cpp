#include <iostream>
#include <fstream>
#include <chrono>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>

void initialize_data(int* data, long int num_elements, long int num_writes, int rank) {
	for (long int k = 0; k < num_writes; ++k) {
		for (long int i = 0; i < num_elements; ++i) {
			data[i + k * num_elements] = i % 10 + rank;
		}
	}
}

using cclock = std::chrono::system_clock;
using sec = std::chrono::duration<double>;

void write_to_file(int* data, long int num_elements, long int num_writes, int rank, std::string time_file) {
	std::string file_name = "file_" + std::to_string(rank) + ".txt";
	int fd = open(file_name.c_str(), O_CREAT | O_WRONLY, 0644);
	std::ofstream file;
	cclock::time_point before;
	if (rank == 0) {
		file.open(time_file, std::fstream::app);
		// for milliseconds, use using ms = std::chrono::duration<double, std::milli>;
		before = cclock::now();
	}

	for (long int i = 0; i < num_writes; ++i) {
		int res = 0;
		long int k = 0;
    	long int num_elements_written;
    	while (k < num_elements) {
        	res = write(fd, data + i * num_elements + k, sizeof(int) * (num_elements - k));
        	num_elements_written = (res / sizeof(int));
        	k += num_elements_written;
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
	long int num_elements, num_writes;
	std::string time_file;
	MPI_Init(&argc, &argv);
	if (argc != 4) {
		std::cerr << "simple write input error" << std::endl;
		MPI_Finalize();
		return 0;
	}
	num_elements = std::atol(argv[1]);
	num_writes = std::atol(argv[2]);
	time_file = argv[3];
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	int* data = new int[num_elements * num_writes];
	initialize_data(data, num_elements, num_writes, rank);
	write_to_file(data, num_elements, num_writes, rank, time_file);
	delete[] data;
	MPI_Finalize();
}
