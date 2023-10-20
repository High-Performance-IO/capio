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


const size_t* N_ELEMS_DATA_BUFS = nullptr;
const size_t* WINDOW_DATA_BUFS = nullptr;
#define DNAME_LENGTH 128

/*
 * From the man getdents:
 * "There is no definition of struct linux_dirent  in  glibc; see NOTES."
 *
 * NOTES section:
 * "[...] you will need to define the linux_dirent or linux_dirent64
 * structure yourself."
 *
 */

struct linux_dirent {
	unsigned long  d_ino;
	off_t          d_off;
	unsigned short d_reclen;
	char           d_name[DNAME_LENGTH + 2];
};

struct linux_dirent64 {
	ino64_t  	   d_ino;
	off64_t        d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char           d_name[DNAME_LENGTH + 1];
};

const static int theoretical_size_dirent64 = sizeof(ino64_t) + sizeof(off64_t) + sizeof(unsigned short) + sizeof(unsigned char) + sizeof(char) * (DNAME_LENGTH + 1);

const static int theoretical_size_dirent = sizeof(unsigned long) + sizeof(off_t) + sizeof(unsigned short) + sizeof(char) * (DNAME_LENGTH + 2);

static inline bool is_absolute(const char* pathname) {
	return (pathname ? (pathname[0]=='/') : false);
}

void get_circular_buffers_info() {
	char* val;
	if (!WINDOW_DATA_BUFS) {
		val = getenv("CAPIO_WINDOW_DATA_BUFS_SIZE");
		if (val)
			WINDOW_DATA_BUFS = new size_t(strtol(val, NULL, 10));
		else
			WINDOW_DATA_BUFS = new size_t(256 * 1024);
		val = getenv("CAPIO_N_ELEMS_DATA_BUFS");
		if (val)
			N_ELEMS_DATA_BUFS = new size_t(strtol(val, NULL, 10));
		else
			N_ELEMS_DATA_BUFS = new size_t(1024);

	}
}


static inline int is_directory(int dirfd) {
	struct stat path_stat;
    if (fstat(dirfd, &path_stat) != 0) {
		std::cerr << "error: stat" << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
		return -1;
	}
    return S_ISDIR(path_stat.st_mode);  // 1 is a directory 
}

static inline int is_directory(const char *path) {
   struct stat statbuf;
   if (stat(path, &statbuf) != 0) {
		std::cerr << "error: stat" << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
		return -1;
   }
   return S_ISDIR(statbuf.st_mode);
}

static inline void err_exit(std::string error_msg, std::ostream& outstream = std::cerr) {
	outstream << "error: " << error_msg << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
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
		err_exit("ftruncate create_shm " + shm_name);
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap create_shm " + shm_name);
	if (close(fd) == -1)
		err_exit("close");
	return p;
}

void* expand_shared_mem(std::string shm_name, long int new_size) {
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
	if (ftruncate(fd, new_size) == -1)
		err_exit("ftruncate expand_shared_mem" + shm_name);
	p = mmap(NULL, new_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
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
		err_exit("ftruncate create shm *fd " + shm_name);
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, *fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap create_shm " + shm_name);
	return p;
}

#endif
