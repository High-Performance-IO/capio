#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "circular_buffer.hpp"

#include <unordered_map>
#include <unordered_set>
#include <set>
#include <string>
#include <iostream>
#include <filesystem>

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

#ifndef __x86_64__
# define _STAT_VER_LINUX        3
#else
# define _STAT_VER_LINUX        1
#endif
#define _STAT_VER                _STAT_VER_LINUX

int (*real_open)(const char* pathname, int flags, ...) = NULL;
ssize_t (*real_read)(int fd, void* buffer, size_t count) = NULL;
ssize_t (*real_write)(int fd, const void* buffer, size_t count) = NULL;
int (*real_close)(int fd) = NULL;
int (*real_access)(const char *pathname, int mode) = NULL;
int (*real_stat)(const char *__restrict pathname, struct stat *__restrict statbuf) = NULL;
int (*real_fstat)(int fd, struct stat *statbuf) = NULL;
int (*real_fxstat)(int ver, int fd, struct stat *statbuf) = NULL;
off_t (*real_lseek) (int fd, off_t offset, int whence) = NULL;
struct dirent* (*real_readdir) (DIR *dirp);
DIR* (*real_opendir)(const char *name);

std::string capio_dir;

static bool is_fstat = true;
int num_writes_batch = 1;
int actual_num_writes = 1;

// initial size for each file (can be overwritten by the user)
const size_t file_initial_size = 1024L * 1024 * 1024 * 4;

/* fd -> (shm*, *offset, file_size, mapped_shm_size )
 * The mapped shm size isn't the the size of the file shm
 * but it's the mapped shm size in the virtual adress space
 * of the server process. The effective size can be greater
 * in a given moment.
 *
 * TODO: offset for now is the size of the file
 */

std::unordered_map<int, std::tuple<void*, size_t*, size_t, size_t>> files;
Circular_buffer<char>* buf_requests;
 
Circular_buffer<size_t>* buf_response;
sem_t* sem_response;
sem_t* sem_write;
int* client_caching_info;
int* caching_info_size;

// fd -> pathname
std::unordered_map<int, std::string> capio_files_descriptors; 
std::unordered_set<std::string> capio_files_paths;

int is_directory(const char *path) {
   struct stat statbuf;
   if (stat(path, &statbuf) != 0) {
		std::cerr << "error: stat" << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
		exit(1);
   }
   return S_ISDIR(statbuf.st_mode);
}

/*
 * This function must be called only once
 *
 */

void mtrace_init(void) {
	char* val;

	val = getenv("GW_BATCH");
	if (val != NULL) {
		num_writes_batch = std::stoi(val);
		if (num_writes_batch <= 0) {
			std::cerr << "error: GW_BATCH variable must be >= 0";
			exit(1);
		}
	}
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
		real_stat = (int (*)(const char *__restrict, struct stat *__restrict)) dlsym(RTLD_NEXT, "__xstat");
		if (NULL == real_stat) {	
			std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;
			exit(1);
		}
	}
	real_fstat = (int (*)(int, struct stat*)) dlsym(RTLD_NEXT, "fstat");
	if (NULL == real_fstat) {	
		real_fxstat = (int (*)(int, int, struct stat*)) dlsym(RTLD_NEXT, "__fxstat");
		if (NULL == real_fxstat) {	
			std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;
			exit(1);
		}
		is_fstat = false;
	}
	real_lseek = (off_t (*)(int, off_t, int)) dlsym(RTLD_NEXT, "lseek");
	if (NULL == real_lseek) {	
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;
		exit(1);
	}
	real_readdir = (struct dirent* (*)(DIR*)) dlsym(RTLD_NEXT, "readdir");
	if (NULL == real_readdir) {	
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;
		exit(1);
	}
	real_opendir = (DIR* (*)(const char*)) dlsym(RTLD_NEXT, "opendir");
	if (NULL == real_opendir) {	
		std::cerr << "Error in `dlsym open`: " << dlerror() << std::endl;
		exit(1);
	}
	sem_response = sem_open(("sem_response_read" + std::to_string(getpid())).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	sem_write = sem_open(("sem_write" + std::to_string(getpid())).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	buf_requests = new Circular_buffer<char>("circular_buffer", 256L * 1024 * 1024, sizeof(char) * 64);
	buf_response = new Circular_buffer<size_t>("buf_response" + std::to_string(getpid()), 256L * 1024 * 1024, sizeof(long int));
	client_caching_info = (int*) create_shm("caching_info" + std::to_string(getpid()), 4096);
	caching_info_size = (int*) create_shm("caching_info_size" + std::to_string(getpid()), sizeof(int));
	*caching_info_size = 0; 
	val = getenv("CAPIO_DIR");
	try {
		if (val == NULL) {
			capio_dir = std::filesystem::canonical(".");	
		}
		else {
			capio_dir = std::filesystem::canonical(val);
		}
	}
	catch (const std::exception& ex){
		std::cout << ex.what() << std::endl;
		exit(1);
	}

	int res = is_directory(capio_dir.c_str());
	if (res == 0) {
		std::cerr << "dir " << capio_dir << " is not a directory" << std::endl;
		exit(1);
	}


}

void add_open_request(const char* pathname, size_t fd) {
	std::string str ("open " + std::to_string(getpid()) + " " + std::to_string(fd) + " " + std::string(pathname));
	const char* c_str = str.c_str();
	buf_requests->write(c_str); //TODO: max upperbound for pathname
}

int add_close_request(int fd) {
	const char* c_str = ("clos " +std::to_string(getpid()) + " "  + std::to_string(fd)).c_str();
	buf_requests->write(c_str);
	return 0;
}

void add_read_request(int fd, size_t count, std::tuple<void*, size_t*, size_t, size_t>& t) {
	std::string str = "read " + std::to_string(getpid()) + " " + std::to_string(fd) + " " + std::to_string(count);
	const char* c_str = str.c_str();
	buf_requests->write(c_str);
	//read response (offest)
	size_t file_size;
	buf_response->read(&file_size);
	std::get<2>(t) = file_size;
	size_t file_shm_size = std::get<3>(t);
	if (file_size > file_shm_size) {
		size_t new_size;
		if (file_size > file_shm_size * 2)
			new_size = file_size;
		else
			new_size = file_shm_size * 2;

		void* p = mremap(std::get<0>(t), file_shm_size, new_size, MREMAP_MAYMOVE);
		if (p == MAP_FAILED)
			err_exit("mremap " + std::to_string(fd));
		std::get<0>(t) = p;
		std::get<3>(t) = new_size;
	}
	return;
}


void add_write_request(int fd, size_t count) {
	char c_str[64];
	*std::get<1>(files[fd]) += count; //works only if there is only one writer at time for each file
	sprintf(c_str, "writ %d %d %ld", getpid(),fd, *std::get<1>(files[fd]));
	if (actual_num_writes == num_writes_batch) {
		buf_requests->write(c_str);
		actual_num_writes = 1;
	}
	else
		++actual_num_writes;
	//sem_wait(sem_response);
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
	std::string path_to_check(pathname);
	bool exists;
	try {
		path_to_check = std::filesystem::canonical(path_to_check);
		exists = true;
	}
	catch (const std::exception& ex) {
		exists = false;
	}
	if (! exists) { 
		if (pathname[0] == '/' || pathname[0] == '.') {
			bool found = false;
			int i = path_to_check.size() - 1;
			while (i >= 0 && !found) {
				found = (path_to_check[i] == '/');
				--i;
			}
			i += 2;
			path_to_check = path_to_check.substr(0, i);
			path_to_check = std::filesystem::canonical(path_to_check);
		}
		else {
			std::string curr_dir = std::filesystem::current_path();
			path_to_check = curr_dir + "/" + curr_dir;
		}
	}
	auto res = std::mismatch(capio_dir.begin(), capio_dir.end(), path_to_check.begin());
	if (res.first == capio_dir.end()) {
		if (capio_dir.size() == path_to_check.size()) {
			std::cerr << "open to the capio_dir" << std::endl;
			exit(1);
		}
		else  {
		//create shm
			int fd;
			void* p = create_shm(pathname, 1024L * 1024 * 1024* 2, &fd);
			add_open_request(pathname, fd);
			size_t* p_offset = create_shm_size_t("offset_" + std::to_string(getpid()) + "_" + std::to_string(fd));
			*p_offset = 0;
			files[fd] = std::make_tuple(p, p_offset, 0, file_initial_size);
			capio_files_descriptors[fd] = pathname;
			capio_files_paths.insert(pathname);
			return fd;
		}
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
		add_close_request(fd);
		capio_files_descriptors.erase(fd);
		return real_close(fd);
	}
	else {
		int res = real_close(fd);
		return res;
	}
}

ssize_t read(int fd, void *buffer, size_t count) {
	if (capio_files_descriptors.find(fd) != capio_files_descriptors.end()) {
		std::tuple<void*, size_t*, size_t, size_t>* t = &files[fd];
		size_t* offset = std::get<1>(*t);
		//bool in_shm = check_cache(fd);
		//if (in_shm) {
			if (*offset + count > std::get<2>(*t))
				add_read_request(fd, count, *t);
			read_shm(std::get<0>(*t), *offset, buffer, count);
		//}
		//else {
			//read_from_disk(fd, offset, buffer, count);
		//}
		*offset = *offset + count;
		return count;
	}
	else { 
		return real_read(fd, buffer, count);
	}
}

ssize_t write(int fd, const  void *buffer, size_t count) {
	if (capio_files_descriptors.find(fd) != capio_files_descriptors.end()) {
		//bool in_shm = check_cache(fd);
		//if (in_shm) {
		size_t file_shm_size = std::get<3>(files[fd]);
		if (*std::get<1>(files[fd]) > file_shm_size) {
			size_t file_size = *std::get<1>(files[fd]);
			size_t new_size;
			if (file_size + count > file_shm_size * 2)
				new_size = file_size + count;
			else
				new_size = file_shm_size * 2;
			std::string shm_name = capio_files_descriptors[fd];
			int fd_shm = shm_open(shm_name.c_str(), O_RDWR,  S_IRUSR | S_IWUSR); 
			if (fd == -1)
				err_exit(" write_shm shm_open " + shm_name);
			if (ftruncate(fd_shm, new_size) == -1)
				err_exit("ftruncate " + shm_name);
			void* p = mremap(std::get<0>(files[fd]), file_size, new_size, MREMAP_MAYMOVE);
			if (p == MAP_FAILED)
				err_exit("mremap " + shm_name);
			close(fd_shm);
		}
		write_shm(std::get<0>(files[fd]), *std::get<1>(files[fd]), buffer, count);
		add_write_request(fd, count); //bottleneck
		//}
		//else {
			//write_to_disk(fd, files[fd].second, buffer, count);
		//}
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
	if (real_fstat == NULL && real_fxstat == NULL)
		mtrace_init();
	if (capio_files_descriptors.find(fd) != capio_files_descriptors.end()) {
		return 0;
	}
	else {
		if (is_fstat)
			return real_fstat(fd, statbuf);
		else
			return real_fxstat(_STAT_VER_LINUX, fd, statbuf);
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
