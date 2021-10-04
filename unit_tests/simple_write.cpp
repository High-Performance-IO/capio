#include <iostream>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>

void initialize_data(int* data, int num_elements, int num_writes, int rank) {
	for (int k = 0; k < num_writes; ++k) {
		for (int i = 0; i < num_elements; ++i) {
			data[i + k * num_elements] = i + k * num_elements + rank;
		}
	}
}

int main (int argc, char** argv) {
	int rank;
	int num_elements, num_writes;
	MPI_Init(&argc, &argv);
	if (argc != 3) {
		std::cout << "simple read input error" << std::endl;
		MPI_Finalize();
		return 0;
	}
	num_elements = std::atoi(argv[1]);
	num_writes = std::atoi(argv[2]);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	int* data = new int[num_elements * num_writes];
	initialize_data(data, num_elements, num_writes, rank);
	std::string file_name = "file_" + std::to_string(rank) + ".txt";
	int fd = open(file_name.c_str(), O_CREAT | O_WRONLY, 0644);
	for (int i = 0; i < num_writes; ++i) {
		int res, k = 0;
    	int num_elements_written;
    	while (k < num_elements) {
        	res = write(fd, data + i * num_elements + k, sizeof(int) * (num_elements - k));
        	num_elements_written = (res / sizeof(int));
        	std::cout << "num_elements_written " << num_elements_written << std::endl;
        	k += num_elements_written;
    	}
	}
	if (close(fd) == -1)
		std::cerr << "process " << rank << ", error closing the file\n";
	delete[] data;
	MPI_Finalize();
}
