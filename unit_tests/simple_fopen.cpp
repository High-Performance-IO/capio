#include <iostream>

#include <stdio.h>
#include <fcntl.h>

int main(int argc, char** argv) {
	FILE* fp = fopen("file_0.txt", "w");
	std::cout << "after fopen" << std::endl;
	int fd = open("file_1.txt", O_CREAT | O_WRONLY, 0644);
	std::cout << "after posix open" << std::endl;
	return 0;

}
