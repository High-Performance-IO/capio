#include <cstring>
#include <iostream>

#include <stdio.h>
#include <fcntl.h>

int main(int argc, char** argv) {
	FILE* fp = fopen("file_0.txt", "w");
	if (fp == NULL) {
		std::cerr << "fopen error: " << strerror(errno) << std::endl;
	}
	std::cout << "after fopen" << std::endl;
	int fd = open("file_1.txt", O_CREAT | O_WRONLY, 0644);
	if (fd == -1) {
		std::cerr << "open error: " << strerror(errno) << std::endl;
	}
	std::cout << "after posix open" << std::endl;
	return 0;

}
