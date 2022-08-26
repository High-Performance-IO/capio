#ifndef CAPIO_COMMON_HPP_
#define CAPIO_COMMON_HPP_

#include <iostream>
#include <string>
#include <cstring>
#include <climits>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

void err_exit(std::string error_msg) {
	std::cerr << "error: " << error_msg << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
	exit(1);
}


void* get_shm(std::string shm_name) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_RDWR, 0); //to be closed
	struct stat sb;
	if (fd == -1)
		err_exit("get_shm shm_open " + shm_name);
	/* Open existing object */
	/* Use shared memory object size as length argument for mmap()
	and as number of bytes to write() */
	if (fstat(fd, &sb) == -1)
		err_exit("fstat " + shm_name);
	p = mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap get_shm " + shm_name);
	if (close(fd) == -1)
		err_exit("close");
	return p;
}

void* get_shm_if_exist(std::string shm_name) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_RDWR, 0); //to be closed
	struct stat sb;
	if (fd == -1) {
		if (errno == ENOENT)
			return nullptr;
		err_exit("get_shm shm_open " + shm_name);
	}
	/* Open existing object */
	/* Use shared memory object size as length argument for mmap()
	and as number of bytes to write() */
	if (fstat(fd, &sb) == -1)
		err_exit("fstat " + shm_name);
	p = mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap get_shm " + shm_name);
	if (close(fd) == -1)
		err_exit("close");
	return p;
}


void* create_shm(std::string shm_name, const long int size) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
	if (fd == -1)
		err_exit("create_shm shm_open " + shm_name);
	if (ftruncate(fd, size) == -1)
		err_exit("ftruncate " + shm_name);
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap create_shm " + shm_name);
	if (close(fd) == -1)
		err_exit("close");
	return p;
}

void* create_shm(std::string shm_name, const long int size, int* fd) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	*fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
	if (*fd == -1)
		err_exit("create_shm 3 args shm_open " + shm_name);
	if (ftruncate(*fd, size) == -1)
		err_exit("ftruncate " + shm_name);
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, *fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap create_shm " + shm_name);
	return p;
}

#endif
