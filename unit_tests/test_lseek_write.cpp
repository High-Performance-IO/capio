
#include <fcntl.h>
#include <unistd.h>

#include "../src/utils/common.hpp"

void read_file() {
	int fd = open("file.txt", O_RDONLY);
	if (fd == -1)
		err_exit("open file.txt in read_file");
	int* array = new int[9];
	if (read(fd, array, 9 * sizeof(int)) != 9 * sizeof(int)) {
		std::cerr << "error read in read_file" << std::endl;
		exit(1);
	}
	for (int i = 0; i < 9; ++i)
		std::cout << "num " << array[i] << std::endl;
	delete[] array;
	if (close(fd) == -1)
		err_exit("close file.txt in read_file");
}

void test_lseek_cur_write() {
	int array[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	int fd = open("file_0_0.txt", O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	if (fd == -1) {
		err_exit("open file_0.txt in test_lseek_write");	
	}
	size_t offset = 0;
	write(fd, array, sizeof(int) * 3);
	offset = sizeof(int) * 3;
	lseek(fd, offset, SEEK_CUR);
	write(fd, array + 6, sizeof(int) * 3);
	lseek(fd, offset, SEEK_SET);
	write(fd, array + 3, sizeof(int) * 3);
	if (close(fd) == -1) {
		err_exit("open file_0.txt in test_lseek_write");	
	}

}

void test_lseek_set_write() {
	int array[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	int fd = open("file_0.txt", O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	if (fd == -1) {
		err_exit("open file_0.txt in test_lseek_write");	
	}
	size_t offset = 0;
	write(fd, array, sizeof(int) * 3);
	offset = sizeof(int) * 6;
	lseek(fd, offset, SEEK_SET);
	write(fd, array + 6, sizeof(int) * 3);
	if (close(fd) == -1) {
		err_exit("open file_0.txt in test_lseek_write");	
	}

	read_file();
}
void test_lseek_read() {

}

int main(int argc, char** aargv) {
	std::cout << "test lseek cur write" << std::endl;
	test_lseek_cur_write();
	//std::cout << "test lseek end write" << std::endl;
	//test_lseek_set_write();
	test_lseek_read();
}
