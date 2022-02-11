#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unordered_map>
#include <unordered_set>
#include <set>
#include <string>
#include <iostream>

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>

#include "utils/common.hpp"

int (*real_open)(const char* pathname, int flags, ...) = NULL;
ssize_t (*real_read)(int fd, void* buffer, size_t count) = NULL;
ssize_t (*real_write)(int fd, const void* buffer, size_t count) = NULL;
int (*real_close)(int fd) = NULL;
int (*real_access)(const char *pathname, int mode) = NULL;
int (*real_stat)(const char *__restrict pathname, struct stat *__restrict statbuf) = NULL;
int (*real_fstat)(int fd, struct stat *statbuf) = NULL;
off_t (*real_lseek) (int fd, off_t offset, int whence) = NULL;
struct dirent* (*real_readdir) (DIR *dirp);
DIR* (*real_opendir)(const char *name);

std::unordered_map<int, std::pair<void*, long int>> files;
circular_buffer buf_requests; 
long int* buf_response;
int i_resp;
sem_t* sem_requests;
sem_t* sem_new_msgs;
sem_t* sem_response;
sem_t* sem_write;
int* client_caching_info;
int* caching_info_size;

// fd -> pathname
std::unordered_map<int, std::string> capio_files_descriptors; 
std::unordered_set<std::string> capio_files_paths;


sem_t* get_sem_requests() {
	return sem_open("sem_requests", 0);
}


/*
 * This function must be called only once
 *
 */

void mtrace_init(void) {
	real_open = (int (*)(const char*, int, ...)) dlsym(RTLD_NEXT, "open");
	if (NULL == real_open) {
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;;
		exit(1);
	}
	real_read = (ssize_t (*)(int, void*, size_t)) dlsym(RTLD_NEXT, "read");
	if (NULL == real_read) {
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;;
		exit(1);
	}
	real_write = (ssize_t (*)(int, const void*, size_t)) dlsym(RTLD_NEXT, "write");
	if (NULL == real_write) {
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;;
		exit(1);
	}
	real_close = (int (*)(int)) dlsym(RTLD_NEXT, "close");
	if (NULL == real_close) {	
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;;
		exit(1);
	}
	real_access = (int (*)(const char*, int)) dlsym(RTLD_NEXT, "access");
	if (NULL == real_access) {	
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;;
		exit(1);
	}
	real_stat = (int (*)(const char *__restrict, struct stat *__restrict)) dlsym(RTLD_NEXT, "stat");
	if (NULL == real_stat) {	
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;;
		exit(1);
	}
	real_fstat = (int (*)(int, struct stat*)) dlsym(RTLD_NEXT, "fstat");
	if (NULL == real_fstat) {	
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;;
		exit(1);
	}
	real_lseek = (off_t (*)(int, off_t, int)) dlsym(RTLD_NEXT, "lseek");
	if (NULL == real_lseek) {	
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;;
		exit(1);
	}
	real_readdir = (struct dirent* (*)(DIR*)) dlsym(RTLD_NEXT, "readdir");
	if (NULL == real_readdir) {	
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;;
		exit(1);
	}
	real_opendir = (DIR* (*)(const char*)) dlsym(RTLD_NEXT, "opendir");
	if (NULL == real_opendir) {	
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;;
		exit(1);
	}
	buf_requests = get_circular_buffer();
	sem_requests = get_sem_requests();
	sem_new_msgs = sem_open("sem_new_msgs", O_RDWR);
	sem_response = sem_open(("sem_response_read" + std::to_string(getpid())).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	sem_write = sem_open(("sem_write" + std::to_string(getpid())).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	buf_response = (long int*) create_shm("buf_response" + std::to_string(getpid()), 4096);
	i_resp = 0;
	client_caching_info = (int*) create_shm("caching_info" + std::to_string(getpid()), 4096);
	caching_info_size = (int*) create_shm("caching_info_size" + std::to_string(getpid()), sizeof(int));
	*caching_info_size = 0; 

}

int add_open_request(const char* pathname) {
	int fd;
	sem_wait(sem_requests);
	std::string str ("open " + std::to_string(getpid()) + " " + std::string(pathname));
	const char* c_str = str.c_str();
	memcpy(((char*)buf_requests.buf) + *buf_requests.i, c_str, strlen(c_str) + 1);
	*buf_requests.i = *buf_requests.i + strlen(c_str) + 1;
	sem_post(sem_requests);
	sem_post(sem_new_msgs);
	//wait for response
	sem_wait(sem_response);
	fd = buf_response[i_resp];
	++i_resp;
	return fd; 
}

int add_close_request(int fd) {
	const char* c_str = ("clos " +std::to_string(getpid()) + " "  + std::to_string(fd)).c_str();
    sem_wait(sem_requests);
	memcpy(((char*)buf_requests.buf) + *buf_requests.i, c_str, strlen(c_str) + 1);
	*buf_requests.i = *buf_requests.i + strlen(c_str) + 1;
	sem_post(sem_requests);
	sem_post(sem_new_msgs);
	return 0;
}

long int add_read_request(int fd, size_t count) {
	std::string str = "read " + std::to_string(getpid()) + " " + std::to_string(fd) + " " + std::to_string(count);
	const char* c_str = str.c_str();
    sem_wait(sem_requests);
	memcpy(((char*)buf_requests.buf) + *buf_requests.i, c_str, strlen(c_str) + 1);
	*buf_requests.i = *buf_requests.i + strlen(c_str) + 1;
	sem_post(sem_requests);
	sem_post(sem_new_msgs);
	//read response (offest)
	sem_wait(sem_response);
	long int offset = buf_response[i_resp];
	++i_resp;
	return offset;
}

void add_write_request(int fd, size_t count) {
	std::string str = "writ " + std::to_string(getpid()) +  " " + std::to_string(fd) + " " + std::to_string(count);
	const char* c_str = str.c_str();    
	sem_wait(sem_requests);
	memcpy(((char*)buf_requests.buf) + *buf_requests.i, c_str, strlen(c_str) + 1);
	*buf_requests.i = *buf_requests.i + strlen(c_str) + 1;
	sem_post(sem_requests);
	sem_post(sem_new_msgs);
	sem_wait(sem_response);
	return;
}

void read_shm(void* shm, long int offset, void* buffer, size_t count) {
	memcpy(buffer, ((char*)shm) + offset, count); 
#ifdef MYDEBUG
		int* tmp = (int*) malloc(count);
		memcpy(tmp, ((char*)shm) + offset, count); 
		for (int i = 0; i < count / sizeof(int); ++i) {
			if (tmp[i] != i % 10) 
				std::cerr << "posix library local read tmp[i] " << tmp[i] << std::endl;
		}
		free(tmp);
#endif

}

void write_shm(void* shm, size_t offset, const void* buffer, size_t count) {	
	memcpy(((char*)shm) + offset, buffer, count); 
	sem_post(sem_write);
}

/*
 * Returns true if the file with file descriptor fd is in shared memory, false
 * if the file is in the disk
 */

bool check_cache(int fd) {
	int i = 0;
	bool found = false;
	while (!found && i < *caching_info_size) {
		if (fd == client_caching_info[i]) {
			found = true;
		}
		else {
			i += 2;
		}
	}
	if (!found) {
		std::cerr << "error check cache: file not found" << std::endl;
		exit(1);
	}
	return client_caching_info[i + 1] == 0;
}

void write_to_disk(const int fd, const int offset, const void* buffer, const size_t count) {
	auto it = capio_files_descriptors.find(fd);
	if (it == capio_files_descriptors.end()) {
		std::cerr << "capio error in write to disk: file descriptor does not exist" << std::endl;
	}
	std::string path = it->second;
	int filesystem_fd = real_open(path.c_str(), O_WRONLY);//TODO: maybe not efficient open in each write and why O_APPEND (without lseek) does not work?
	if (filesystem_fd == -1) {
		std::cerr << "capio client error: impossible write to disk capio file " << fd << std::endl;
		exit(1);
	}
	lseek(filesystem_fd, offset, SEEK_SET);
	ssize_t res = real_write(filesystem_fd, buffer, count);
	if (res == -1) {
		err_exit("capio error writing to disk capio file ");
	}	
	if ((size_t)res != count) {
		std::cerr << "capio error write to disk: only " << res << " bytes written of " << count << std::endl; 
		exit(1);
	}
	if (real_close(filesystem_fd) == -1) {
		std::cerr << "capio impossible close file " << filesystem_fd << std::endl;
		exit(1);
	}
}

void read_from_disk(int fd, int offset, void* buffer, size_t count) {
	auto it = capio_files_descriptors.find(fd);
	if (it == capio_files_descriptors.end()) {
		std::cerr << "capio error in write to disk: file descriptor does not exist" << std::endl;
	}
	std::string path = it->second;
	int filesystem_fd = real_open(path.c_str(), O_RDONLY);//TODO: maybe not efficient open in each read
	if (filesystem_fd == -1) {
		err_exit("capio client error: impossible to open file for read from disk"); 
	}
	off_t res_lseek = real_lseek(filesystem_fd, offset, SEEK_SET);
	if (res_lseek == -1) {
		err_exit("capio client error: lseek in read from disk");
	}
	ssize_t res_read = real_read(filesystem_fd, buffer, count);
	if (res_read == -1) {
		err_exit("capio client error: read in read from disk");
	}
	if (real_close(filesystem_fd) == -1) {
		err_exit("capio client error: close in read from disk");
	}
}


extern "C" {

int open(const char *pathname, int flags, ...) {
	if (real_open == NULL)
		mtrace_init();
	const char* prefix = "file_";
	const char* prefix_2 = "output_file_";
	if (strncmp("file_", pathname, strlen(prefix)) == 0 || strncmp("output_file_", pathname, strlen(prefix_2)) == 0) {
		//create shm
		int fd = add_open_request(pathname);
		files[fd] = std::pair<void*, int>(get_shm(pathname), 0);
		capio_files_descriptors[fd] = pathname;
		capio_files_paths.insert(pathname);
		return fd;
	}
	else {
		mode_t mode = 0;
		if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
			va_list ap;
			va_start(ap, flags);
			mode = va_arg(ap, mode_t);
			va_end(ap);
		}
		int fd = real_open(pathname, flags, mode);
		return fd;
	}
}



int close(int fd) {
	if (real_close == NULL)
		mtrace_init();
	if (capio_files_descriptors.find(fd) != capio_files_descriptors.end()) {
		//TODO: only the CAPIO deamon will free the shared memory
		int res = add_close_request(fd);
		capio_files_descriptors.erase(fd);
		return res;
	}
	else {
		int res = real_close(fd);
		return res;
	}
}

ssize_t read(int fd, void *buffer, size_t count) {
	if (capio_files_descriptors.find(fd) != capio_files_descriptors.end()) {
		long int offset = add_read_request(fd, count);
		bool in_shm = check_cache(fd);
		if (in_shm) {
			read_shm(files[fd].first, offset, buffer, count);
		}
		else {
			read_from_disk(fd, offset, buffer, count);
		}
		files[fd].second = offset;
		return count;
	}
	else { 
		return real_read(fd, buffer, count);
	}
}

ssize_t write(int fd, const  void *buffer, size_t count) {
	if (capio_files_descriptors.find(fd) != capio_files_descriptors.end()) {
		add_write_request(fd, count);
		if (files.find(fd) == files.end()) { //only for debug
			std::cerr << "error write to invalid adress" << std::endl;
			exit(1);
		}
		bool in_shm = check_cache(fd);
		if (in_shm) {
			write_shm(files[fd].first, files[fd].second, buffer, count);
		}
		else {
			write_to_disk(fd, files[fd].second, buffer, count);
		}
		files[fd].second += count;
		return count;
	}
	else {
		return real_write(fd, buffer, count);
	}
}

int access(const char *pathname, int mode) {
	if (real_access == NULL)
		mtrace_init();
	if (capio_files_paths.find(pathname) != capio_files_paths.end()) {
		return 0;
	}
	else {
		return real_access(pathname, mode);
	}
}

int stat(const char *__restrict pathname, struct stat *__restrict statbuf) {
	if (real_stat == NULL)
		mtrace_init();
	if (capio_files_paths.find(pathname) != capio_files_paths.end()) {
		return 0;	
	}
	else {
		return real_stat(pathname, statbuf);
	}
}

int fstat(int fd, struct stat *statbuf) { 
	if (real_fstat == NULL)
		mtrace_init();
	if (capio_files_descriptors.find(fd) != capio_files_descriptors.end()) {
		return 0;
	}
	else {
		return real_fstat(fd, statbuf);
	}
}


off_t lseek(int fd, off_t offset, int whence) { 
	if (capio_files_descriptors.find(fd) != capio_files_descriptors.end()) {
		return 0;
	}
	else {
		return real_lseek(fd, offset, whence);
	}
}

DIR* opendir(const char* name) {
	return real_opendir(name);
}

struct dirent* readdir(DIR* dirp) { 
	return real_readdir(dirp);
}


}
