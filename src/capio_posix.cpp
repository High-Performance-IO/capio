#include "circular_buffer.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <set>
#include <string>
#include <iostream>
#include <filesystem>
#include <climits>

#include <stdlib.h>
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
#include <syscall.h>
#include <sys/uio.h>
#include <sys/ioctl.h>

#include <libsyscall_intercept_hook_point.h>

#include "utils/common.hpp"
#include "capio_file.hpp"

std::string capio_dir;

int num_writes_batch = 1;
int actual_num_writes = 1;

// initial size for each file (can be overwritten by the user)
const size_t file_initial_size = 1024L * 1024 * 1024 * 4;

/* fd -> (shm*, *offset, mapped_shm_size, offset_upper_bound, Capio_file, file status flags, file_descriptor_flags)
 * The mapped shm size isn't the the size of the file shm
 * but it's the mapped shm size in the virtual adress space
 * of the server process. The effective size can be greater
 * in a given moment.
 *
 */

std::unordered_map<int, std::tuple<void*, off64_t*, off64_t, off64_t, Capio_file, int, int>>* files = nullptr;
Circular_buffer<char>* buf_requests;
 
Circular_buffer<off_t>* buf_response;
sem_t* sem_response;
sem_t* sem_write;
int* client_caching_info;
int* caching_info_size;

// fd -> (normalized) pathname
std::unordered_map<int, std::string>* capio_files_descriptors = nullptr; 
std::unordered_set<std::string>* capio_files_paths = nullptr;

std::unordered_map<int, std::pair<std::vector<int>, bool>>* fd_copies = nullptr;
static bool first_call = true;
static int last_pid = 0;
static bool stat_enabled = true;
static bool dup2_enabled = true;
static bool fork_enabled = true;

// -------------------------  utility functions:
static bool is_absolute(const char* pathname) {
	return (pathname ? (pathname[0]=='/') : false);
}
static int is_directory(int dirfd) {
	struct stat path_stat;
	stat_enabled = false;
    if (fstat(dirfd, &path_stat) != 0) {
		std::cerr << "error: stat" << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
		stat_enabled=true;
		return -1;
	}
	stat_enabled = true;
    return S_ISDIR(path_stat.st_mode);  // 1 is a directory 
}
static int is_directory(const char *path) {
   struct stat statbuf;
   stat_enabled = false;
   if (stat(path, &statbuf) != 0) {
		std::cerr << "error: stat" << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
		stat_enabled=true;
		return -1;
   }
   stat_enabled=true;
   return S_ISDIR(statbuf.st_mode);
}
static blkcnt_t get_nblocks(off64_t file_size) {
	if (file_size % 4096 == 0)
		return file_size / 512;
	
	return file_size / 512 + 8;
}
static std::string get_dir_path(const char* pathname, int dirfd) {
	char proclnk[128];
	char dir_pathname[PATH_MAX];
	sprintf(proclnk, "/proc/self/fd/%d", dirfd);
    ssize_t r = readlink(proclnk, dir_pathname, PATH_MAX);
    if (r < 0){
    	printf("failed to readlink\n");
		return "";
    }
    dir_pathname[r] = '\0';
	return dir_pathname;
}


#if !defined(EXTRA_LEN_PRINT_ERROR)
#define EXTRA_LEN_PRINT_ERROR   512
#endif
#define CAPIO_DBG(str, ...) \
  print_prefix(str, "DBG:", ##__VA_ARGS__)

static inline void print_prefix(const char* str, const char* prefix, ...) {
    va_list argp;
    char * p=(char *)malloc(strlen(str)+strlen(prefix)+EXTRA_LEN_PRINT_ERROR);
    if (!p) {
		perror("malloc");
		fprintf(stderr,"FATAL ERROR in print_prefix\n");
        return;
    }
    strcpy(p,prefix);
    strcpy(p+strlen(prefix), str);
    va_start(argp, prefix);
    vfprintf(stderr, p, argp);
    va_end(argp);
    free(p);
}
// utility functions  -------------------------


/*
 * This function must be called only once
 *
 */

void mtrace_init(void) {
	CAPIO_DBG("mtrace init\n");
	if (fd_copies == nullptr) {
	CAPIO_DBG("mtrace init creating data structures\n");
		fd_copies = new std::unordered_map<int, std::pair<std::vector<int>, bool>>;
		capio_files_descriptors = new std::unordered_map<int, std::string>; 
		capio_files_paths = new std::unordered_set<std::string>;

		files = new std::unordered_map<int, std::tuple<void*, off64_t*, off64_t, off64_t, Capio_file, int, int>>;
	}
	char* val;
	first_call = false;
	last_pid = getpid();
	val = getenv("CAPIO_DIR");
	stat_enabled = false;
	try {
		if (val == NULL) {
			capio_dir = std::filesystem::canonical(".");	
		}
		else {
			capio_dir = std::filesystem::canonical(val);
		}
	}
	catch (const std::exception& ex) {
		exit(1);
	}
	int res = is_directory(capio_dir.c_str());
	if (res == 0) {
		std::cerr << "dir " << capio_dir << " is not a directory" << std::endl;
		exit(1);
	}
	val = getenv("GW_BATCH");
	if (val != NULL) {
		num_writes_batch = std::stoi(val);
		if (num_writes_batch <= 0) {
			std::cerr << "error: GW_BATCH variable must be >= 0";
			exit(1);
		}
	}
	sem_response = sem_open(("sem_response_read" + std::to_string(getpid())).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	sem_write = sem_open(("sem_write" + std::to_string(getpid())).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	buf_requests = new Circular_buffer<char>("circular_buffer", 1024 * 1024, sizeof(char) * 600);
	buf_response = new Circular_buffer<off_t>("buf_response" + std::to_string(getpid()), 256L * 1024 * 1024, sizeof(off_t));
	client_caching_info = (int*) create_shm("caching_info" + std::to_string(getpid()), 4096);
	caching_info_size = (int*) create_shm("caching_info_size" + std::to_string(getpid()), sizeof(int));
	*caching_info_size = 0; 

	stat_enabled = true;

}

std::string create_absolute_path(const char* pathname) {	
	char* abs_path = (char*) malloc(sizeof(char) * PATH_MAX);
	stat_enabled = false;
	char* res_realpath = realpath(pathname, abs_path);
	stat_enabled = true;
	if (res_realpath == NULL) {
		int i = strlen(pathname);
		bool found = false;
		bool no_slash = true;
		char* pathname_copy = (char*) malloc(sizeof(char) * (strlen(pathname) + 1));
		strcpy(pathname_copy, pathname);
		while (i >= 0 && !found) {
			if (pathname[i] == '/') {
				no_slash = false;
				pathname_copy[i] = '\0';
				abs_path = realpath(pathname_copy, NULL);	
				if (abs_path != NULL)
					found = true;
			}
			--i;
		}
		if (no_slash) {
			getcwd(abs_path, PATH_MAX);
			int len = strlen(abs_path);
			abs_path[len] = '/';
			abs_path[len + 1] = '\0';
			strcat(abs_path, pathname);
			return abs_path;
		}
		if (found) {
			++i;
			strncpy(pathname_copy, pathname + i, strlen(pathname) - i + 1);
			strcat(abs_path, pathname_copy); 
			free(pathname_copy);
		}
		else {
			free(pathname_copy);
			return "";
		}
	}
	std::string res_path(abs_path);
	free(abs_path);
	return res_path;
}



void add_open_request(const char* pathname, size_t fd) {
	std::string str ("open " + std::to_string(getpid()) + " " + std::to_string(fd) + " " + std::string(pathname));
	const char* c_str = str.c_str();
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char)); //TODO: max upperbound for pathname
}

int add_close_request(int fd) {
	std::string msg = "clos " +std::to_string(getpid()) + " "  + std::to_string(fd);
	const char* c_str = msg.c_str();
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
	return 0;
}

off64_t add_read_request(int fd, off64_t count, std::tuple<void*, off64_t*, off64_t, off64_t, Capio_file, int , int>& t) {
	std::string str = "read " + std::to_string(getpid()) + " " + std::to_string(fd) + " " + std::to_string(count);
	const char* c_str = str.c_str();
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
	//read response (offest)
	off64_t offset_upperbound;
	buf_response->read(&offset_upperbound);
	std::get<3>(t) = offset_upperbound;
	size_t file_shm_size = std::get<2>(t);
	size_t end_of_read;
	end_of_read = *std::get<1>(t) + count;
	if (end_of_read > offset_upperbound)
		end_of_read = offset_upperbound;
	if (end_of_read > file_shm_size) {
		size_t new_size;
		if (end_of_read > file_shm_size * 2)
			new_size = end_of_read;
		else
			new_size = file_shm_size * 2;

		void* p = mremap(std::get<0>(t), file_shm_size, new_size, MREMAP_MAYMOVE);
		if (p == MAP_FAILED)
			err_exit("mremap " + std::to_string(fd));
		std::get<0>(t) = p;
		std::get<2>(t) = new_size;
	}
	return offset_upperbound;
}


void add_write_request(int fd, off64_t count) { //da modifcare con capio_file sia per una normale scrittura sia per quando si fa il batch
	char c_str[64];
	long int old_offset = *std::get<1>((*files)[fd]);
	*std::get<1>((*files)[fd]) += count; //works only if there is only one writer at time for each file
	if (actual_num_writes == num_writes_batch) {
		sprintf(c_str, "writ %d %d %ld %ld", getpid(),fd, old_offset, count);
		std::get<4>((*files)[fd]).insert_sector(old_offset, old_offset + count);
		buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
		actual_num_writes = 1;
	}
	else
		++actual_num_writes;
	//sem_wait(sem_response);
	return;
}

void read_shm(void* shm, long int offset, void* buffer, off64_t count) {
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

void write_shm(void* shm, size_t offset, const void* buffer, off64_t count) {	
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
	auto it = capio_files_descriptors->find(fd);
	if (it == capio_files_descriptors->end()) {
		std::cerr << "capio error in write to disk: file descriptor does not exist" << std::endl;
	}
	std::string path = it->second;
	int filesystem_fd = open(path.c_str(), O_WRONLY);//TODO: maybe not efficient open in each write and why O_APPEND (without lseek) does not work?
	if (filesystem_fd == -1) {
		std::cerr << "capio client error: impossible write to disk capio file " << fd << std::endl;
		exit(1);
	}
	lseek(filesystem_fd, offset, SEEK_SET);
	ssize_t res = write(filesystem_fd, buffer, count);
	if (res == -1) {
		err_exit("capio error writing to disk capio file ");
	}	
	if ((size_t)res != count) {
		std::cerr << "capio error write to disk: only " << res << " bytes written of " << count << std::endl; 
		exit(1);
	}
	if (close(filesystem_fd) == -1) {
		std::cerr << "capio impossible close file " << filesystem_fd << std::endl;
		exit(1);
	}
	//SEEK_HOLE SEEK_DATA
}

void read_from_disk(int fd, int offset, void* buffer, size_t count) {
	auto it = capio_files_descriptors->find(fd);
	if (it == capio_files_descriptors->end()) {
		std::cerr << "capio error in write to disk: file descriptor does not exist" << std::endl;
	}
	std::string path = it->second;
	int filesystem_fd = open(path.c_str(), O_RDONLY);//TODO: maybe not efficient open in each read
	if (filesystem_fd == -1) {
		err_exit("capio client error: impossible to open file for read from disk"); 
	}
	off_t res_lseek = lseek(filesystem_fd, offset, SEEK_SET);
	if (res_lseek == -1) {
		err_exit("capio client error: lseek in read from disk");
	}
	ssize_t res_read = read(filesystem_fd, buffer, count);
	if (res_read == -1) {
		err_exit("capio client error: read in read from disk");
	}
	if (close(filesystem_fd) == -1) {
		err_exit("capio client error: close in read from disk");
	}
}

/*
 * The lseek() function shall fail if:
 *
 *     EBADF  The fildes argument is not an open file descriptor.
 *
 *     EINVAL The whence argument is not a proper value, or  the  resulting
 *            file  offset would be negative for a regular file, block spe‐
 *            cial file, or directory.
 *
 *     EOVERFLOW
 *            The resulting file offset would be a value  which  cannot  be
 *            represented correctly in an object of type off_t.
 *
 *     ESPIPE The  fildes  argument  is  associated  with  a pipe, FIFO, or
 *            socket.
*/

//TODO: EOVERFLOW is not adressed
off_t capio_lseek(int fd, off64_t offset, int whence) { 
	auto it = fd_copies->find(fd);
	if (it != fd_copies->end()) {
		if (! it->second.second) {
			fd = it->second.first[0];
		}
		std::tuple<void*, off64_t*, off64_t, off64_t, Capio_file, int, int>* t = &(*files)[fd];
		off64_t* file_offset = std::get<1>(*t);
		if (whence == SEEK_SET) {
			if (offset >= 0) {
				*file_offset = offset;
				char c_str[64];
				sprintf(c_str, "seek %d %d %zu", getpid(),fd, *file_offset);
				buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
				off64_t offset_upperbound;
				buf_response->read(&offset_upperbound);
				std::get<3>(*t) = offset_upperbound;
				return *file_offset;
			}
			else {
				errno = EINVAL;
				return -1;
			}
		}
		else if (whence == SEEK_CUR) {
			off64_t new_offset = *file_offset + offset;
			if (new_offset >= 0) {
				*file_offset = new_offset;
				char c_str[64];
				sprintf(c_str, "seek %d %d %zu", getpid(),fd, *file_offset);
				buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
				off64_t offset_upperbound;
				buf_response->read(&offset_upperbound);
				std::get<3>(*t) = offset_upperbound;
				return *file_offset;
			}
			else {
				errno = EINVAL;
				return -1;
			}
		}
		else if (whence == SEEK_END) {
			//works only in batch mode or if we know tha size of the file
			/*long int file_size = std::get<4>(*t).get_file_size();
			off_t new_offset = file_size + offset;
			if (new_offset >=0)
				*file_offset = new_offset;
			else {
				errno = EINVAL;
				return -1;
			}
			*/
			char c_str[64];
			off64_t file_size;
			sprintf(c_str, "send %d %d", getpid(),fd);
			buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
			buf_response->read(&file_size);
			off64_t offset_upperbound;
			offset_upperbound = file_size;
			*file_offset = file_size + offset;	
			std::get<3>(*t) = offset_upperbound;
			return *file_offset;
		}
		else if (whence == SEEK_DATA) {
				char c_str[64];
				sprintf(c_str, "sdat %d %d %zu", getpid(),fd, *file_offset);
				buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
				off64_t offset_upperbound;
				buf_response->read(&offset_upperbound);
				std::get<3>(*t) = offset_upperbound;
				buf_response->read(file_offset);
				return *file_offset;

		}
		else if (whence == SEEK_HOLE) {
				char c_str[64];
				sprintf(c_str, "shol %d %d %zu", getpid(),fd, *file_offset);
				buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
				off64_t offset_upperbound;
				buf_response->read(&offset_upperbound);
				std::get<3>(*t) = offset_upperbound;
				buf_response->read(file_offset);
				return *file_offset;

		}
		else {
			errno = EINVAL;
			return -1;
		}
		
	}
	else {
		return -2;
	}
}


int capio_openat(int dirfd, const char* pathname, int flags) {
	CAPIO_DBG("capio_openat %s\n", pathname);
	if (first_call || last_pid != getpid())
		mtrace_init();
	std::string path_to_check;
	if(is_absolute(pathname)) {
		path_to_check = pathname;
		CAPIO_DBG("capio_openat absolute %s\n", path_to_check.c_str());
	}
	else {
		if(dirfd == AT_FDCWD) {
			path_to_check = create_absolute_path(pathname);
			if (path_to_check.length() == 0)
				return -2;
			CAPIO_DBG("capio_openat AT_FDCWD %s\n", path_to_check.c_str());
		}
		else {
			if (is_directory(dirfd) != 1)
				return -2;
			std::string dir_path = get_dir_path(pathname, dirfd);
			if (dir_path.length() == 0)
				return -2;
			path_to_check = dir_path + "/" + pathname;
			CAPIO_DBG("capio_openat with dirfd %s\n", path_to_check.c_str());
		}
	}
	auto res = std::mismatch(capio_dir.begin(), capio_dir.end(), path_to_check.begin());
	if (res.first == capio_dir.end()) {
		if (capio_dir.size() == path_to_check.size()) {
			return -2;
			std::cerr << "ERROR: open to the capio_dir " << path_to_check << std::endl;
			exit(1);

		}
		else  {
		//create shm
			char shm_name[512];
			int i = 0;
			while (i < path_to_check.length()) {
				if (path_to_check[i] == '/' && i > 0)
					shm_name[i] = '_';
				else
					shm_name[i] = path_to_check[i];
				++i;
			}
			shm_name[i] = '\0';
			printf("%s\n", shm_name);
			int fd;
			void* p = create_shm(shm_name, 1024L * 1024 * 1024* 2, &fd);
			add_open_request(shm_name, fd);
			off64_t* p_offset = create_shm_off64_t("offset_" + std::to_string(getpid()) + "_" + std::to_string(fd));
			*p_offset = 0;
			files->insert({fd, std::make_tuple(p, p_offset, file_initial_size, 0, Capio_file(), flags, 0)});
			(*capio_files_descriptors)[fd] = shm_name;
			(*fd_copies)[fd] = std::make_pair(std::vector<int>(), true);
			capio_files_paths->insert(path_to_check);
			if ((flags & O_APPEND) == O_APPEND) {
				capio_lseek(fd, 0, SEEK_END);
			}
			CAPIO_DBG("capio_openat returning %d\n", fd);
			return fd;
		}
	}
	else {
		return -2;
	}
}

ssize_t capio_write(int fd, const  void *buffer, size_t count) {
	auto it = fd_copies->find(fd);
	if (it != fd_copies->end()) {
		if (! it->second.second) {
			fd = it->second.first[0];
		}
		if (count > SSIZE_MAX) {
			std::cerr << "Capio does not support writes bigger then SSIZE_MAX yet" << std::endl;
			exit(1);
		}
		off64_t count_off = count;
		//bool in_shm = check_cache(fd);
		//if (in_shm) {
		off64_t file_shm_size = std::get<2>((*files)[fd]);
		if (*std::get<1>((*files)[fd]) + count_off > file_shm_size) {
			off64_t file_size = *std::get<1>((*files)[fd]);
			off64_t new_size;
			if (file_size + count_off > file_shm_size * 2)
				new_size = file_size + count_off;
			else
				new_size = file_shm_size * 2;
			std::string shm_name = (*capio_files_descriptors)[fd];
			int fd_shm = shm_open(shm_name.c_str(), O_RDWR,  S_IRUSR | S_IWUSR); 
			if (fd == -1)
				err_exit(" write_shm shm_open " + shm_name);
			if (ftruncate(fd_shm, new_size) == -1)
				err_exit("ftruncate " + shm_name);
			void* p = mremap(std::get<0>((*files)[fd]), file_size, new_size, MREMAP_MAYMOVE);
			if (p == MAP_FAILED)
				err_exit("mremap " + shm_name);
			close(fd_shm);
		}
		write_shm(std::get<0>((*files)[fd]), *std::get<1>((*files)[fd]), buffer, count_off);
		add_write_request(fd, count_off); //bottleneck
		//}
		//else {
			//write_to_disk(fd, (*files)[fd].second, buffer, count);
		//}
		return count;
	}
	else {
		return -2;	
	}

}

int capio_close(int fd) {
	CAPIO_DBG("capio_close %d %d\n", getpid(), fd);
	if (first_call || last_pid != getpid())
		mtrace_init();
	auto it = fd_copies->find(fd);
	if (it != fd_copies->end()) {
		if (! it->second.second) {
			fd = it->second.first[0];
		CAPIO_DBG("capio_close debug 0\n");
		}
		add_close_request(fd);
		auto& copies = (*fd_copies)[fd].first;
		if (copies.size() > 0) {
		CAPIO_DBG("capio_close debug 1\n");
			int master_fd = copies[0];
			(*files)[master_fd] = (*files)[fd];
			(*capio_files_descriptors)[master_fd] = (*capio_files_descriptors)[fd];
			(*fd_copies)[master_fd].second = true;
			auto& master_copies = (*fd_copies)[master_fd].first;
			master_copies.erase(master_copies.begin());
			for (size_t i = 1; i < copies.size(); ++i) {
		CAPIO_DBG("capio_close debug 2\n");
				int fd_copy = copies[i];
				master_copies.push_back(fd_copy);
				auto& copy_vec = (*fd_copies)[fd_copy].first;
				copy_vec.erase(copy_vec.begin());
				copy_vec.push_back(master_fd);
			}

		}
		CAPIO_DBG("capio_close debug 3\n");
		capio_files_descriptors->erase(fd);
		CAPIO_DBG("capio_close debug 4\n");
		fd_copies->erase(fd);
		CAPIO_DBG("capio_close debug 5\n");
		files->erase(fd);
		CAPIO_DBG("capio_close returning %d %d\n", getpid(), fd);
		return close(fd);
	}
	else {
		CAPIO_DBG("capio_close returning -2 %d %d\n", getpid(), fd);
		return -2;
	}
}

ssize_t capio_read(int fd, void *buffer, size_t count) {
	auto it = fd_copies->find(fd);
	if (it != fd_copies->end()) {
		if (! it->second.second) {
			fd = it->second.first[0];
		}
		if (count >= SSIZE_MAX) {
			std::cerr << "capio does not support read bigger then SSIZE_MAX yet" << std::endl;
			exit(1);
		}
		off64_t count_off = count;
		std::tuple<void*, off64_t*, off64_t, off64_t, Capio_file, int, int>* t = &(*files)[fd];
		off64_t* offset = std::get<1>(*t);
		//bool in_shm = check_cache(fd);
		//if (in_shm) {
		off64_t bytes_read;
			if (*offset + count_off > std::get<3>(*t)) {
				off64_t end_of_read;
				end_of_read = add_read_request(fd, count_off, *t);
				bytes_read = end_of_read - *offset;
				if (bytes_read > count_off)
					bytes_read = count_off;
			}
			else
				bytes_read = count_off;
			//std:: cout << "count_off " << count_off << std::endl; 
			//std:: cout << "offset " << *offset << std::endl; 
			//std::cout << "before read shm " << bytes_read << std::endl;
			read_shm(std::get<0>(*t), *offset, buffer, bytes_read);
		//}
		//else {
			//read_from_disk(fd, offset, buffer, count);
		//}
		*offset = *offset + bytes_read;
		return bytes_read;
	}
	else { 
		return -2;
	}
}



ssize_t capio_writev(int fd, const struct iovec* iov, int iovcnt) {
	auto it = fd_copies->find(fd);
	if (it != fd_copies->end()) {
		if (! it->second.second) {
			fd = it->second.first[0];
		}
		ssize_t tot_bytes = 0;
		ssize_t res = 0;
		int i = 0;
		while (i < iovcnt && res >= 0) {
			res = capio_write(fd, iov[i].iov_base, iov[i].iov_len);
			tot_bytes += res;
			++i;
		}
		if (res == -1)
			return -1;
		else
			return tot_bytes;
	}
	else
		return -2;

}

int capio_fcntl(int fd, int cmd, int arg) {
  auto it = fd_copies->find(fd);
  if (it != fd_copies->end()) {
    CAPIO_DBG("capio_fcntl\n");
    if (!it->second.second) {
      fd = it->second.first[0];
    }
    switch (cmd) {
    case F_GETFD: {
      return std::get<6>((*files)[fd]);
      break;
    }
    case F_SETFD: {
      std::get<6>((*files)[fd]) = arg;
      return 0;
      break;
    }
    case F_GETFL: {
      return std::get<5>((*files)[fd]);

      break;
    }
    case F_SETFL: {
      std::get<5>((*files)[fd]) = arg;
      return 0;
      break;
    }
    case F_DUPFD_CLOEXEC: {
      int dev_fd = open("/dev/null", O_RDONLY);
      if (dev_fd == -1)
        err_exit("open /dev/null");
      int res = fcntl(dev_fd, F_DUPFD_CLOEXEC, arg); //
      close(dev_fd);
      if (res != -1) {
        (*fd_copies)[fd].first.push_back(res);
        (*fd_copies)[res].first.push_back(fd);
        (*fd_copies)[res].second = false;
      }
      return res;
      break;
    }
    default:
      std::cerr << "fcntl with cmd " << cmd << " is not yet supported"
                << std::endl;
      exit(1);
    }
  } else
    return -2;
}
ssize_t capio_fgetxattr(int fd, const char* name, void* value, size_t size) {
	auto it = fd_copies->find(fd);
	if (it != fd_copies->end()) {
		if (! it->second.second) {
			fd = it->second.first[0];
		}
		if (strcmp(name, "system.posix_acl_access") == 0) {
			errno = ENODATA;	
			return -1;
		}
		else {
			std::cerr << "fgetxattr with name " << name << " is not yet supporte in CAPIO" << std::endl;
			exit(1);
		}
	}
	else
		return -2;

}

ssize_t capio_flistxattr(int fd, char* list, ssize_t size) {
	auto it = fd_copies->find(fd);
	if (it != fd_copies->end()) {
		if (!it->second.second) {
			fd = it->second.first[0];
		}
		if (list == NULL && size == 0) {
			return 0;
		}
		else {
			std::cerr << "flistxattr is not yet supported in CAPIO" << std::endl;
			exit(1);
		}
	}
	else
		return -2;
		
}

int capio_ioctl(int fd, unsigned long request) {
	auto it = fd_copies->find(fd);
	if (it != fd_copies->end()) {
		if (!it->second.second) {
			fd = it->second.first[0];
		}
		return 0;
	}
	else
		return -2;
	
}

void capio_exit_group(int status) {
	int pid = getpid();
	std::string str = "exig " + std::to_string(pid);
	const char* c_str = str.c_str();
	CAPIO_DBG("capio exit group captured%d\n", pid);
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
	return;
}

/*
 * Precondition: absolute_path must contain an absolute path
 *
 */

int capio_lstat(std::string absolute_path, struct stat* statbuf) {
	CAPIO_DBG("capio_lstat %s\n", absolute_path.c_str());
	if (first_call || last_pid != getpid())
		mtrace_init();
	auto res = std::mismatch(capio_dir.begin(), capio_dir.end(), absolute_path.begin());
	if (res.first == capio_dir.end()) {
		if (capio_dir.size() == absolute_path.size()) {
			//it means capio_dir is equals to absolute_path
			return -2;

		}
		char normalized_path[2048];
		int i = 0;
		while (absolute_path[i] != '\0') {
			if (absolute_path[i] == '/' && i > 0)
				normalized_path[i] = '_';
			else
				normalized_path[i] = absolute_path[i];
			++i;
		}
		normalized_path[i] = '\0';
		CAPIO_DBG("capio_lstat sending msg to server\n");
		std::string msg = "stat " + std::to_string(getpid()) + " " + normalized_path;
		const char* c_str = msg.c_str();
		buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
		off64_t file_size;
		buf_response->read(&file_size);
		if (file_size == -1) {
			errno = ENOENT;
			return -1;
		}
		statbuf->st_dev = 100;

		
		std::hash<std::string> hash;		
		statbuf->st_ino = hash(normalized_path);

		statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0644 regular file 
		statbuf->st_nlink = 1;
		statbuf->st_uid = 0; // root 
		statbuf->st_gid = 0; // root
		statbuf->st_rdev = 0;
		statbuf->st_size = file_size;
		CAPIO_DBG("lstat file_size=%ld\n",file_size);
		statbuf->st_blksize = 4096;
		if (file_size < 4096)
			statbuf->st_blocks = 8;
		else
			statbuf->st_blocks = get_nblocks(file_size);
		struct timespec time;
		time.tv_sec = 1;
		time.tv_nsec = 1;
		statbuf->st_atim = time;
		statbuf->st_mtim = time;
		statbuf->st_ctim = time;
		return 0;
	}
	else
		return -2;

}

int capio_lstat_wrapper(const char* path, struct stat* statbuf) {
	CAPIO_DBG("capio_lstat_wrapper\n");
	std::string absolute_path;	
	absolute_path = create_absolute_path(path);
	if (absolute_path.length() == 0)
		return -2;
	return capio_lstat(absolute_path, statbuf);	
}

int capio_fstat(int fd, struct stat* statbuf) {
	if (first_call || last_pid != getpid())
		mtrace_init();
	auto it = fd_copies->find(fd);
	if (it != fd_copies->end()) {
		if (!it->second.second) {
			fd = it->second.first[0];
		}
		std::string msg = "fsta " + std::to_string(getpid()) + " " + std::to_string(fd);
		buf_requests->write(msg.c_str(), (strlen(msg.c_str()) + 1) * sizeof(char));
		off64_t file_size;
		buf_response->read(&file_size);
		statbuf->st_dev = 100;

		std::hash<std::string> hash;		
		statbuf->st_ino = hash((*capio_files_descriptors)[fd]);

	    statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0644 regular file 
		statbuf->st_nlink = 1;
		statbuf->st_uid = 0; // root
		statbuf->st_gid = 0; // root
		statbuf->st_rdev = 0;
		statbuf->st_size = file_size;
		CAPIO_DBG("capio_fstat file_size=%ld\n", file_size);
		statbuf->st_blksize = 4096;
		if (file_size < 4096)
			statbuf->st_blocks = 8;
		else
			statbuf->st_blocks = get_nblocks(file_size);
		struct timespec time;
		time.tv_sec = 1;
		time.tv_nsec = 1;
		statbuf->st_atim = time;
		statbuf->st_mtim = time;
		statbuf->st_ctim = time;
		return 0;
	}
	else
		return -2;
	
}




int capio_fstatat(int dirfd, const char* pathname, struct stat* statbuf, int flags) {
	if ((flags & AT_EMPTY_PATH) == AT_EMPTY_PATH) {
		if(dirfd == AT_FDCWD) { // operate on currdir
			char* curr_dir = get_current_dir_name(); 
			return capio_lstat(curr_dir, statbuf);
			free(curr_dir);
		}
		else { // operate on dirfd
		// in this case dirfd can refer to any type of file
			if (strlen(pathname) == 0)
				return capio_fstat(dirfd, statbuf);
			else {
				//TODO: set errno	
				return -1;
			}
		}

	}

	int res = -1;

	if (!is_absolute(pathname)) {
		if (dirfd == AT_FDCWD) { 
		// pathname is interpreted relative to currdir
			res = capio_lstat_wrapper(pathname, statbuf);		
		}
		else { 
			if (is_directory(dirfd) != 1)
				return -2;
			std::string dir_path = get_dir_path(pathname, dirfd);
			if (dir_path.length() == 0)
				return -2;
			std::string path = dir_path + "/" + pathname;
			std::cout << "fstat path " << path << std::endl;
			res = capio_lstat(path, statbuf);
		}
	}
	else { //dirfd is ignored
		res = capio_lstat(std::string(pathname), statbuf);
	}
	return res;

}


int capio_creat(const char* pathname, mode_t mode) {
	CAPIO_DBG("capio_creat %s\n", pathname);
	int res = capio_openat(AT_FDCWD, pathname, O_CREAT | O_WRONLY | O_TRUNC);

	CAPIO_DBG("capio_creat %s returning %d\n", pathname, res);
	return res;
}

int is_capio_file(std::string abs_path) {
	auto it = std::mismatch(capio_dir.begin(), capio_dir.end(), abs_path.begin());
	if (it.first == capio_dir.end()) 
		return 0;
	else 
		return -1;
}

int capio_access(const char* pathname, int mode) {
	if (mode == F_OK) {
		std::string abs_pathname = create_absolute_path(pathname);
		abs_pathname = create_absolute_path(pathname);
		if (abs_pathname.length() == 0) {
			errno = ENONET;
			return -1;
		}
		return is_capio_file(abs_pathname);
	}
	else if ((mode | X_OK) == X_OK) {
		return -1;
	}
	else
		return 0;
}

int capio_file_exists(std::string path) {
	char normalized_path[PATH_MAX];
	normalized_path[0] = '/';
	for (int i = 1; i < path.length(); ++i) {
		if (path[i] == '/')
			normalized_path[i] = '_';
		else
			normalized_path[i] = path[i];
	}

	std::string msg = "accs " + std::string(normalized_path);
	off64_t res;
	buf_requests->write(msg.c_str(), (strlen(msg.c_str()) + 1) * sizeof(char));
	buf_response->read(&res);
	return res;
}

int capio_faccessat(int dirfd, const char* pathname, int mode, int flags) {
	int res;
	if (!is_absolute(pathname)) {
		if (dirfd == AT_FDCWD) { 
		// pathname is interpreted relative to currdir
			res = capio_access(pathname, mode);		
		}
		else { 
			if (is_directory(dirfd) != 1) 
				return -2;
			std::string dir_path = get_dir_path(pathname, dirfd);
			if (dir_path.length() == 0)
				return -2;
			std::string path = dir_path + "/" + pathname;
			auto it = std::mismatch(capio_dir.begin(), capio_dir.end(), path.begin());
			if (it.first == capio_dir.end()) {
				if (capio_dir.size() == path.size()) {
					std::cerr << "ERROR: unlink to the capio_dir " << path << std::endl;
					exit(1);
				}
				res = capio_file_exists(path);

			}
			else
				res = -2;
		}
	}
	else { //dirfd is ignored
		res = capio_access(pathname, mode);
	}
	return res;
}

int capio_unlink_abs(std::string abs_path) {
	int res;
	auto it = std::mismatch(capio_dir.begin(), capio_dir.end(), abs_path.begin());
	if (it.first == capio_dir.end()) {
		if (capio_dir.size() == abs_path.size()) {
			std::cerr << "ERROR: unlink to the capio_dir " << abs_path << std::endl;
			exit(1);
		}
		int pid = getpid();
		char normalized_path[PATH_MAX];
		normalized_path[0] = '/';
		for (int i = 1; i < abs_path.length(); ++i) {
			if (abs_path[i] == '/')
				normalized_path[i] = '_';
			else
				normalized_path[i] = abs_path[i];
		}
		normalized_path[abs_path.length()] = '\0';
		std::string msg = "unlk " + std::to_string(pid) + " " + std::string(normalized_path);
		buf_requests->write(msg.c_str(), (strlen(msg.c_str()) + 1) * sizeof(char));
		off64_t res_unlink;
	    buf_response->read(&res_unlink); 	
		res = res_unlink;
		if (res == -1)
			errno = ENOENT;
	}
	else {
		res = -2;
	}
	return res;
}

int capio_unlink(const char* pathname) {
	std::string abs_path = create_absolute_path(pathname);
	if (abs_path.length() == 0)
		return -2;
	int res = capio_unlink_abs(abs_path);
	return res;

}

int capio_unlinkat(int dirfd, const char* pathname, int flags) {
	CAPIO_DBG("capio_unlinkat\n");
	
	int res;
	if (!is_absolute(pathname)) {
		if (dirfd == AT_FDCWD) { 
		// pathname is interpreted relative to currdir
			res = capio_unlink(pathname);		
		}
		else { 
			if (is_directory(dirfd) != 1)
				return -2;
			std::string dir_path = get_dir_path(pathname, dirfd);
			if (dir_path.length() == 0)
				return -2;
			std::string path = dir_path + "/" + pathname;
			CAPIO_DBG("capio_unlinkat path=%s\n",path.c_str());
			
			res = capio_unlink_abs(path);
		}
	}
	else { //dirfd is ignored
		res = capio_unlink_abs(pathname);
	}
	return res;
}

int capio_fchown(int fd, uid_t owner, gid_t group) {
	if (files->find(fd) == files->end())
		return -2;
	else
		return 0;
}

int capio_fchmod(int fd, mode_t mode) {
	if (files->find(fd) == files->end())
		return -2;
	else
		return 0;
}

int capio_dup(int fd) {
	int res;
	auto it = fd_copies->find(fd);
	CAPIO_DBG("capio_dup\n");
	if (it != fd_copies->end()) {
		if (!it->second.second) {
			fd = it->second.first[0];
		}
		CAPIO_DBG("handling capio_dup\n");
		res = open("/dev/null", O_WRONLY);
		if (res == -1)
			err_exit("open in capio_dup");
		(*fd_copies)[fd].first.push_back(res);
		(*fd_copies)[res].first.push_back(fd);
		(*fd_copies)[res].second = false;
		CAPIO_DBG("handling capio_dup returning res %d\n", res);
	}
	else
		res = -2;
	return res;
}

int capio_dup2(int fd, int fd2) {
	int res;
	auto it = fd_copies->find(fd);
	CAPIO_DBG("capio_dup 2\n");
	if (it != fd_copies->end()) {
		if (!it->second.second) {
			fd = it->second.first[0];
		}
		CAPIO_DBG("handling capio_dup2\n");
		dup2_enabled = false;
		res = dup2(fd, fd2);
		dup2_enabled = true;
		if (res == -1)
			return -1;
		(*fd_copies)[fd].first.push_back(res);
		(*fd_copies)[res].first.push_back(fd);
		(*fd_copies)[res].second = false;
		CAPIO_DBG("handling capio_dup returning res %d\n", res);
	}
	else
		res = -2;
	return res;
}

void copy_parent_files() {
	CAPIO_DBG("Im process %d and my parent is %d\n", getpid(), getppid());
	std::string msg = "clon " + std::to_string(getppid()) + " " + std::to_string(getpid());
	buf_requests->write(msg.c_str(), (strlen(msg.c_str()) + 1) * sizeof(char));
}

pid_t capio_fork() {
	fork_enabled = false;
	CAPIO_DBG("fork captured\n");
	pid_t pid = fork();
	if (pid == 0) { //child
		first_call = true;
		mtrace_init();
		copy_parent_files();
		return 0;
	}
	fork_enabled = true;
	return pid;

}

pid_t capio_clone(int (*fn)(void *), void *stack, int flags, void *arg) {
	fork_enabled = false;
	CAPIO_DBG("clone captured\n");
	pid_t pid = fork();
	if (pid == 0) { //child
		for (auto& it : *fd_copies) 
			std::cout << "child key " << it.first << std::endl;
		first_call = true;
		mtrace_init();
		copy_parent_files();
		CAPIO_DBG("returning from clone\n");
		fork_enabled = true;
		return 0;
	}
		for (auto& it : *fd_copies) 
			std::cout << "parent key " << it.first << std::endl;
	fork_enabled = true;
	return pid;
}

static int hook(long syscall_number, long arg0, long arg1, long arg2, long arg3,
                long arg4, long arg5, long *result) {
  int hook_ret_value = 1;
  int res = 0;
  switch (syscall_number) {

  case SYS_open:
    hook_ret_value = 1;
    break;

  case SYS_openat: {
    int dirfd = static_cast<int>(arg0);
    const char *pathname = reinterpret_cast<const char *>(arg1);
    int flags = static_cast<int>(arg2);
    int res = capio_openat(dirfd, pathname, flags);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_write: {
    int fd = static_cast<int>(arg0);
    const void *buf = reinterpret_cast<const void *>(arg1);
    size_t count = static_cast<size_t>(arg2);
    		if (!stat_enabled)
                            return -2;
    stat_enabled = false;
    CAPIO_DBG("write captured %d %d\n", getpid(), fd);
	if (first_call || last_pid != getpid())
		mtrace_init();
    CAPIO_DBG("fd copies size %d %d\n",getpid(),fd_copies->size());
    //CAPIO_DBG("files size %d %d\n",getpid(),files->size());
    CAPIO_DBG("capio_files_paths size %d %d\n",getpid(),capio_files_paths->size());
	for (auto& it : *fd_copies) 
		CAPIO_DBG("child key %d %d\n",getpid(), it.first); 
		
	stat_enabled = true;
    
    ssize_t res = capio_write(fd, buf, count);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_read: {
    int fd = static_cast<int>(arg0);
    void *buf = reinterpret_cast<void *>(arg1);
    size_t count = static_cast<size_t>(arg2);
    ssize_t res = capio_read(fd, buf, count);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_close: {
    int fd = static_cast<int>(arg0);
    int res = capio_close(fd);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_lseek: {
    int fd = static_cast<int>(arg0);
    off_t offset = static_cast<off_t>(arg1);
    int whence = static_cast<int>(arg2);
    off_t res = capio_lseek(fd, offset, whence);
    CAPIO_DBG("seek %d %d\n", getpid(), fd);
    //			for (auto& it : fd_copies)
    //				CAPIO_DBG("child key %d\n",it.first);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_writev: {
    int fd = static_cast<int>(arg0);
    const struct iovec *iov = reinterpret_cast<const struct iovec *>(arg1);
    int iovcnt = static_cast<int>(arg2);
    ssize_t res = capio_writev(fd, iov, iovcnt);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
	break;
  }

  case SYS_fcntl: {
    int fd = arg0;
    int cmd = arg1;
    int arg = arg2;
    int res = capio_fcntl(fd, cmd, arg);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }

    break;
  }

    // TO BE IMPLEMENTED
  case SYS_lgetxattr:
  case SYS_getxattr: {
    CAPIO_DBG("capio_*xattr\n");
    errno = ENODATA;
    *result = -errno;
    hook_ret_value = 0;
    break;
  }

  case SYS_fgetxattr: {
    int fd = arg0;
    const char *name = reinterpret_cast<const char *>(arg1);
    void *value = reinterpret_cast<void *>(arg2);
    size_t size = arg3;
    ssize_t res = capio_fgetxattr(fd, name, value, size);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_flistxattr: {
    int fd = arg0;
    char *list = reinterpret_cast<char *>(arg1);
    size_t size = arg2;
    ssize_t res = capio_flistxattr(fd, list, size);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_ioctl: {
    int fd = arg0;
    unsigned long request = arg1;
    int res = capio_ioctl(fd, request);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_exit_group: {
    int status = arg0;
    capio_exit_group(status);
    hook_ret_value = 1;
    break;
  }
  case SYS_lstat: {
    const char *path = reinterpret_cast<const char *>(arg0);
    struct stat *buf = reinterpret_cast<struct stat *>(arg1);
    if (stat_enabled) {
      res = capio_lstat_wrapper(path, buf);
      if (res != -2) {
        *result = (res < 0 ? -errno : res);
        hook_ret_value = 0;
      }
    }
    break;
  }
  case SYS_stat: {
    const char *path = reinterpret_cast<const char *>(arg0);
    struct stat *buf = reinterpret_cast<struct stat *>(arg1);
    if (stat_enabled) {
      res = capio_lstat_wrapper(path, buf);
      if (res != -2) {
        *result = (res < 0 ? -errno : res);
        hook_ret_value = 0;
      }
    }
    break;
  }

  case SYS_fstat: {
    int fd = arg0;
    struct stat *buf = reinterpret_cast<struct stat *>(arg1);
    if (stat_enabled) {
      res = capio_fstat(fd, buf);
      if (res != -2) {
        *result = (res < 0 ? -errno : res);
        hook_ret_value = 0;
      }
    }
    break;
  }

  case SYS_newfstatat: {
    int dirfd = arg0;
    const char *pathname = reinterpret_cast<const char *>(arg1);
    struct stat *statbuf = reinterpret_cast<struct stat *>(arg2);
    int flags = arg3;
    if (stat_enabled) {
      res = capio_fstatat(dirfd, pathname, statbuf, flags);
      if (res != -2) {
        *result = (res < 0 ? -errno : res);
        hook_ret_value = 0;
      }
    }
    break;
  }

  case SYS_creat: {
    const char *pathname = reinterpret_cast<const char *>(arg0);
    mode_t mode = arg1;
    res = capio_creat(pathname, mode);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_access: {
    const char *pathname = reinterpret_cast<const char *>(arg0);
    int mode = arg1;
    res = capio_access(pathname, mode);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_faccessat: {
    int dirfd = arg0;
    const char *pathname = reinterpret_cast<const char *>(arg1);
    int mode = arg2;
    int flags = arg3;
    res = capio_faccessat(dirfd, pathname, mode, flags);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_unlink: {
    const char *pathname = reinterpret_cast<const char *>(arg0);
    res = capio_unlink(pathname);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_unlinkat: {
    int dirfd = arg0;
    const char *pathname = reinterpret_cast<const char *>(arg1);
    int flag = arg2;
    res = capio_unlinkat(dirfd, pathname, flag);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_fchown: {
    int fd = arg0;
    uid_t owner = arg1;
    gid_t group = arg2;
    res = capio_fchown(fd, owner, group);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_fchmod: {
    int fd = arg0;
    mode_t mode = arg1;
    res = capio_fchmod(fd, mode);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_dup: {
    int fd = arg0;
    res = capio_dup(fd);
    if (res != -2) {
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    }
    break;
  }

  case SYS_dup2: {
    int fd = arg0;
    int fd2 = arg1;
    if (dup2_enabled) {
      res = capio_dup2(fd, fd2);
      if (res != -2) {
        *result = (res < 0 ? -errno : res);
        hook_ret_value = 0;
      }
    } else
      res = -2;
    break;
  }

  case SYS_fork: {
    CAPIO_DBG("fork before captured\n");
    if (fork_enabled) {
      res = capio_fork();
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    } else
      res = -2;
    break;
  }

  case SYS_clone: {
    int (*fn)(void *) = reinterpret_cast<int (*)(void *)>(arg0);
    void *stack = reinterpret_cast<void *>(arg1);
    int flags = arg2;
    void *arg = reinterpret_cast<void *>(arg3);
    // pid_t* parent_tid = reinterpret_cast<pid_t*>(arg4);
    // void* tls = reinterpret_cast<void*>(arg5);
    // pid_t* child_tid = reinterpret_cast<pid_t*>(arg6);
    if (fork_enabled) {
      res = capio_clone(fn, stack, flags, arg);
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    } else
      res = -2;
    break;
  }

  default:
    hook_ret_value = 1;
  }
  return hook_ret_value;
}

static __attribute__((constructor)) void
init(void)
{
	// Set up the callback function
	intercept_hook_point = hook;
}

