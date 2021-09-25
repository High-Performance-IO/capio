#include <iostream>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>

int sum_all(int *data, int num_elements, int num_reads) {
	int sum = 0;
	for (int k = 0; k < num_reads; ++k) {
		for (int i = 0; i < num_elements; ++i) {
				sum += data[i + k * num_elements];
		}
	}
	return sum;
}

int main (int argc, char** argv) {
	int rank;
	int num_elements, num_reads;
	MPI_Init(&argc, &argv);
	if (argc != 3) {
		std::cout << "simple read input error" << std::endl;
		MPI_Finalize();
		return 0;
	}
	num_elements = std::atoi(argv[1]);
	num_reads = std::atoi(argv[2]);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	int* data = new int[num_elements * num_reads];
	std::string file_name = "file_" + std::to_string(rank) + ".txt";
	int fd = open(file_name.c_str(), O_RDONLY, 0644);
	for (int i = 0; i < num_reads; ++i) {
		if (read(fd, data + i * num_elements, num_elements * sizeof(int)) != num_elements  * sizeof(int)) {
			std::cerr << "process " << rank << ", error reading the file\n";
			delete[] data;
			MPI_Finalize();
			return 0;
		}
	}
	int sum = sum_all(data, num_elements, num_reads);
	std::cout << "process " << rank << ", sum = " << sum << "\n";
	if (close(fd) == -1)
		std::cerr << "process " << rank << ", error closing the file\n";
	delete[] data;
	MPI_Finalize();
}

