#include <iostream>
#include <cstring>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>


void initialize_array(int* array, int size, int init_val) {
	for (int i = 0; i < size; ++i)
		array[i] = init_val + i;
}

int main (int argc, char** argv) {
	int fd = open("file_0_0.txt", O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	if (fd == -1) {
		std::cerr << "error open file_0_0.txt" << std::endl;
		exit(1);
	}
	int* array1 = new int[10];
	int* array2 = new int[8];
	int* array3 = new int[2];
	
	initialize_array(array1, 10, 0);	
	initialize_array(array2, 8, 10);	
	initialize_array(array3, 2, 18);	

	struct iovec iov[3];
	iov[0].iov_base = array1;
	iov[0].iov_len = 10 * sizeof(int);
	iov[1].iov_base = array2;
	iov[1].iov_len = 8 * sizeof(int);
	iov[2].iov_base = array3;
	iov[2].iov_len = 2 * sizeof(int);
	
	ssize_t res = writev(fd, iov, 3);
	if (res == -1) {
		std::cerr << "error writev: " << strerror(errno) << std::endl;
		exit(1);
	}
	else if (res != 20 * sizeof(int)) {
		std::cerr << "error writev: wrote " << res << " bytes instead of 20" << std::endl;
		exit(1);
	}
	delete[] array1;
	delete[] array2;
	delete[] array3;

	if (close(fd) == -1) {
		std::cerr << "error close file_0_0.txt" << std::endl;
	}
}
