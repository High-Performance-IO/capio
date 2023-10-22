
#include <fcntl.h>
#include <unistd.h>

#include "circular_buffer/common.hpp"

void read_file() {
	int fd = open("file_0_0.txt", O_RDONLY);
	if (fd == -1)
		err_exit("open file.txt in read_file");
	int* array = new int[9];
	if (read(fd, array, 3 * sizeof(int)) != 3 * sizeof(int)) {
		std::cerr << "error read in read_file" << std::endl;
		exit(1);
	}
	size_t offset = 3 * sizeof(int);
	lseek(fd, offset, SEEK_CUR);
	if (read(fd, array + 6, 3 * sizeof(int)) != 3 * sizeof(int)) {
		std::cerr << "error read in read_file" << std::endl;
		exit(1);
	}
	lseek(fd, offset, SEEK_SET);
	if (read(fd, array + 3, 3 * sizeof(int)) != 3 * sizeof(int)) {
		std::cerr << "error read in read_file" << std::endl;
		exit(1);
	}
	for (int i = 0; i < 9; ++i)
		std::cout << "num " << array[i] << std::endl;
	delete[] array;
	if (close(fd) == -1)
		err_exit("close file.txt in read_file");
}


int main(int argc, char** argv) {
	read_file();
}
