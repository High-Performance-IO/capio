#include "circular_buffer.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <set>
#include <string>
#include <iostream>
#include <filesystem>
#include <climits>
#include <atomic>
#include <algorithm>

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

struct spinlock {
  std::atomic<bool> lock_ = {0};

  void lock() noexcept {
    for (;;) {
      // Optimistically assume the lock is free on the first try
      if (!lock_.exchange(true, std::memory_order_acquire)) {
        return;
      }
      // Wait for lock to be released without generating cache misses
      while (lock_.load(std::memory_order_relaxed)) {
        // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
        // hyper-threads
        __builtin_ia32_pause();
      }
    }
  }

  bool try_lock() noexcept {
    // First do a relaxed load to check if lock is free in order to prevent
    // unnecessary cache misses if someone does while(!try_lock())
    return !lock_.load(std::memory_order_relaxed) &&
           !lock_.exchange(true, std::memory_order_acquire);
  }

  void unlock() noexcept {
    lock_.store(false, std::memory_order_release);
  }
};

//struct spinlock* sl = nullptr;
//struct spinlock clone_sl;

std::string* capio_dir = nullptr;

int num_writes_batch = 1;
int actual_num_writes = 1;

// initial size for each file (can be overwritten by the user)
const size_t file_initial_size = 1024L * 1024 * 1024 * 4;

/* fd -> (shm*, *offset, *mapped_shm_size, *offset_upper_bound, file status flags, file_descriptor_flags)
 * The mapped shm size isn't the the size of the file shm
 * but it's the mapped shm size in the virtual adress space
 * of the server process. The effective size can be greater
 * in a given moment.
 *
 */

std::unordered_map<int, std::tuple<void*, off64_t*, off64_t*, off64_t*, int, int>>* files = nullptr;
Circular_buffer<char>* buf_requests = nullptr;
 
std::unordered_map<int, Circular_buffer<off_t>*>* bufs_response = nullptr;
//sem_t* sem_response;
sem_t* sem_family = nullptr;
sem_t* sem_first_call = nullptr;
sem_t* sem_clone = nullptr;
sem_t* sem_tmp = nullptr;
std::unordered_map<int, sem_t*>* sems_write;
int* client_caching_info;
int* caching_info_size;

// fd -> (normalized) pathname
std::unordered_map<int, std::string>* capio_files_descriptors = nullptr; 
std::unordered_set<std::string>* capio_files_paths = nullptr;

std::set<int>* first_call = nullptr;
static long int parent_tid = 0;
std::unordered_map<long int, bool>* stat_enabled = nullptr; //TODO: protect with a semaphore
static bool dup2_enabled = true;
static bool fork_enabled = true;
bool thread_created = false;

// -------------------------  utility functions:
static bool is_absolute(const char* pathname) {
	return (pathname ? (pathname[0]=='/') : false);
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
    	fprintf(stderr, "failed to readlink\n");
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
    char p[256];
    strcpy(p,prefix);
    strcpy(p+strlen(prefix), str);
    va_start(argp, prefix);
    vfprintf(stderr, p, argp);
    va_end(argp);
    fflush(stderr);
}
// utility functions  -------------------------

int* get_fd_snapshot() {
	std::string shm_name = "capio_snapshot_" + std::to_string(syscall(SYS_gettid));
	int* fd_shm = (int*) get_shm_if_exist(shm_name);
	return fd_shm;
}

void initialize_from_snapshot(int* fd_shm) {
	int i = 0;
	std::string shm_name;
	int fd;
	off64_t* p_shm;
	char* path_shm;
	#ifdef CAPIOLOG
		CAPIO_DBG("initialize_from_snapshot \n");
	#endif
	std::string pid = std::to_string(syscall(SYS_gettid));
	while ((fd = fd_shm[i]) != -1) {
		shm_name = "capio_snapshot_path_" + pid + "_" + std::to_string(fd);
		path_shm = (char*) get_shm(shm_name);
		(*capio_files_descriptors)[fd] = path_shm;
		capio_files_paths->insert(path_shm);
		std::get<0>((*files)[fd]) = get_shm(path_shm);
		munmap(path_shm, PATH_MAX);
		if (shm_unlink(shm_name.c_str()) == -1) {
			err_exit("shm_unlink snapshot " + shm_name);
		}
		shm_name = "capio_snapshot_" + pid + "_" + std::to_string(fd);
		p_shm = (off64_t*) get_shm(shm_name);
		std::string shm_name_offset = "offset_" + pid + "_" + std::to_string(fd);
		std::get<1>((*files)[fd]) = (off64_t*) create_shm(shm_name_offset, sizeof(off64_t));
		*std::get<1>((*files)[fd]) = p_shm[1];
		std::get<2>((*files)[fd]) = new off64_t;
		*std::get<2>((*files)[fd]) = p_shm[2];
		std::get<3>((*files)[fd]) = new off64_t;
		*std::get<3>((*files)[fd]) = p_shm[3];
		std::get<4>((*files)[fd]) = p_shm[4];
		std::get<5>((*files)[fd]) = p_shm[5];
		munmap(p_shm, 6 * sizeof(off64_t));
		if (shm_unlink(shm_name.c_str()) == -1) {
			err_exit("shm_unlink snapshot " + shm_name);
		}
		++i;
	}
	shm_name = "capio_snapshot_" + pid;
	if (shm_unlink(shm_name.c_str()) == -1) {
		err_exit("shm_unlink snapshot " + shm_name);
	}
	
	#ifdef CAPIOLOG
		CAPIO_DBG("initialize_from_snapshot ending\n");
	#endif
}

/*
 * This function must be called only once
 *
 */

void mtrace_init(void) {
	int my_tid = syscall(SYS_gettid);
	if (first_call == nullptr)
		first_call = new std::set<int>();
	first_call->insert(my_tid);
	sem_post(sem_first_call);
	if (parent_tid == my_tid) {
		#ifdef CAPIOLOG
			CAPIO_DBG("sem wait parent before %ld %ld\n", parent_tid, my_tid);
			CAPIO_DBG("sem_family %ld\n", sem_family);
		#endif
		sem_wait(sem_family);
		#ifdef CAPIOLOG
			CAPIO_DBG("sem wait parent after\n");
		#endif
		sem_post(sem_clone);
		//clone_sl.unlock();
		return;
	}
	(*stat_enabled)[my_tid] = false;

	if (capio_files_descriptors == nullptr) {
		#ifdef CAPIOLOG
		CAPIO_DBG("init data_structures\n");
		#endif
		capio_files_descriptors = new std::unordered_map<int, std::string>; 
		capio_files_paths = new std::unordered_set<std::string>;

		files = new std::unordered_map<int, std::tuple<void*, off64_t*, off64_t*, off64_t*, int, int>>;

		int* fd_shm = get_fd_snapshot();
		if (fd_shm != nullptr) {
			initialize_from_snapshot(fd_shm);
		}
	}
	char* val;
	if (capio_dir == nullptr) {
	val = getenv("CAPIO_DIR");
	try {
		if (val == NULL) {
			capio_dir = new std::string(std::filesystem::canonical("."));	
		}
		else {
			capio_dir = new std::string(std::filesystem::canonical(val));
		}
	}
	catch (const std::exception& ex) {
		exit(1);
	}
	int res = is_directory(capio_dir->c_str());
	if (res == 0) {
		std::cerr << "dir " << capio_dir << " is not a directory" << std::endl;
		exit(1);
	}
	}
	val = getenv("GW_BATCH");
	if (val != NULL) {
		num_writes_batch = std::stoi(val);
		if (num_writes_batch <= 0) {
			std::cerr << "error: GW_BATCH variable must be >= 0";
			exit(1);
		}
	}
//	sem_response = sem_open(("sem_response_read" + std::to_string(my_tid)).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
		if (sem_tmp == nullptr)
			sem_tmp = sem_open(("capio_sem_tmp_" + std::to_string(syscall(SYS_gettid))).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 1);
	#ifdef CAPIOLOG
	CAPIO_DBG("before second lock\n");
	#endif
	sem_wait(sem_tmp);
	#ifdef CAPIOLOG
	CAPIO_DBG("after second lock\n");
	#endif
	if (sems_write == nullptr)
		sems_write = new std::unordered_map<int, sem_t*>();
	sem_t* sem_write = sem_open(("sem_write" + std::to_string(my_tid)).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	sems_write->insert(std::make_pair(my_tid, sem_write));
	if (buf_requests == nullptr)
		buf_requests = new Circular_buffer<char>("circular_buffer", 1024 * 1024, sizeof(char) * 600);
	if (bufs_response == nullptr)
		bufs_response = new std::unordered_map<int, Circular_buffer<off_t>*>();
	Circular_buffer<off_t>* p_buf_response = new Circular_buffer<off_t>("buf_response" + std::to_string(my_tid), 256L * 1024 * 1024, sizeof(off_t));
	bufs_response->insert(std::make_pair(my_tid, p_buf_response));
	client_caching_info = (int*) create_shm("caching_info" + std::to_string(my_tid), 4096);
	caching_info_size = (int*) create_shm("caching_info_size" + std::to_string(my_tid), sizeof(int));
	*caching_info_size = 0; 
	sem_post(sem_tmp);
	if (thread_created) {

	#ifdef CAPIOLOG
	CAPIO_DBG("thread created init\n");
	#endif
		std::string msg = "clon " + std::to_string(parent_tid) + " " + std::to_string(my_tid);
		buf_requests->write(msg.c_str(), (strlen(msg.c_str()) + 1) * sizeof(char));
		sem_post(sem_family);
	#ifdef CAPIOLOG
	CAPIO_DBG("thread created init end\n");
	#endif
	}
	std::string msg = "hand " + std::to_string(my_tid) + " " + std::to_string(getpid());
	buf_requests->write(msg.c_str());
	(*stat_enabled)[my_tid] = true;
	#ifdef CAPIOLOG
	CAPIO_DBG("ending mtrace init\n");
	CAPIO_DBG("CAPIO directory: %s\n", capio_dir->c_str());
	#endif
}

//* fd -> (shm*, *offset, *mapped_shm_size, *offset_upper_bound, file status flags, file_descriptor_flags)
//std::unordered_map<int, std::tuple<void*, off64_t*, off64_t*, off64_t*, int, int>>* files = nullptr;
//std::unordered_map<int, std::string>* capio_files_descriptors = nullptr; 
//std::unordered_set<std::string>* capio_files_paths = nullptr;
void crate_snapshot() {
	int fd, status_flags, fd_flags;
	off64_t offset, mapped_shm_size, offset_upper_bound;
	off64_t* p_shm;
	int* fd_shm;
	char* path_shm;
	std::string pid = std::to_string(syscall(SYS_gettid));
	int n_fd = files->size();
	if (n_fd == 0)
		return;
	fd_shm = (int*)create_shm("capio_snapshot_" + pid, n_fd * sizeof(int));
	#ifdef CAPIOLOG
		CAPIO_DBG("creating snapshots\n");
	#endif
	int i = 0;
	for (auto& p : *files) {
		fd = p.first;	
		p_shm = (off64_t*) create_shm("capio_snapshot_" + pid + "_" + std::to_string(fd), 6 * sizeof(off64_t));
	#ifdef CAPIOLOG
		CAPIO_DBG("creating snapshot fd%d\n", fd);
	#endif
		fd_shm[i] = fd;
		offset = *std::get<1>(p.second);
		mapped_shm_size = *std::get<2>(p.second);
		offset_upper_bound = *std::get<3>(p.second);
		status_flags = std::get<4>(p.second);
		fd_flags = std::get<5>(p.second);
		p_shm[0] = fd;
		p_shm[1] = offset;
		p_shm[2] = mapped_shm_size;
		p_shm[3] = offset_upper_bound;
		p_shm[4] = status_flags;
		p_shm[5] = fd_flags;
		path_shm = (char*) create_shm("capio_snapshot_path_" + pid + "_" + std::to_string(fd), PATH_MAX * sizeof(char));
		strcpy(path_shm, (*capio_files_descriptors)[fd].c_str());
		++i;
	}
	fd_shm[i] = -1;
	return;
}

std::string create_absolute_path(const char* pathname) {	
	char* abs_path = (char*) malloc(sizeof(char) * PATH_MAX);
	long int my_tid = syscall(SYS_gettid);
	(*stat_enabled)[my_tid] = false;
	char* res_realpath = realpath(pathname, abs_path);
	(*stat_enabled)[my_tid] = true;
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
	std::string str ("open " + std::to_string(syscall(SYS_gettid)) + " " + std::to_string(fd) + " " + std::string(pathname));
	const char* c_str = str.c_str();
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char)); //TODO: max upperbound for pathname
}

int add_close_request(int fd) {
	std::string msg = "clos " +std::to_string(syscall(SYS_gettid)) + " "  + std::to_string(fd);
	const char* c_str = msg.c_str();
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
	return 0;
}

int add_mkdir_request(std::string pathname) {
	std::string msg = "mkdi " + std::to_string(syscall(SYS_gettid)) + " " + pathname;
	const char* c_str = msg.c_str();
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
	off64_t res_tmp;	
	int res;
	(*bufs_response)[syscall(SYS_gettid)]->read(&res_tmp);
	if (res_tmp == 1)
		res = -1;
	else
		res = 0;
	return res;
}

int request_mkdir(std::string path_to_check) {
	int res;
auto res_mismatch = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());
	if (res_mismatch.first == capio_dir->end()) {
		if (capio_dir->size() == path_to_check.size()) {
			return -2;
			std::cerr << "ERROR: open to the capio_dir " << path_to_check << std::endl;
			exit(1);

		}
		else  {
			if(capio_files_paths->find(path_to_check) != capio_files_paths->end()) {
			errno = EEXIST;
			return -1;
			}
			res = add_mkdir_request(path_to_check);
			if (res == 0)
				capio_files_paths->insert(path_to_check);
			#ifdef CAPIOLOG
			CAPIO_DBG("capio_mkdir returning %d\n", res);
			#endif
			return res;
		}
	}
	else {
	#ifdef CAPIOLOG
		CAPIO_DBG("capio_mkdir returning -2\n");
	#endif
		return -2;
	}
}

int capio_mkdir(const char* pathname, mode_t mode) {
	int res = 0;
	std::string path_to_check;
	if(is_absolute(pathname)) {
		path_to_check = pathname;
		#ifdef CAPIOLOG
		CAPIO_DBG("capio_mkdir absolute %s\n", path_to_check.c_str());
		#endif
	}
	else {
		path_to_check = create_absolute_path(pathname);
		if (path_to_check.length() == 0)
			return -2;
		#ifdef CAPIOLOG
		CAPIO_DBG("capio_mkdir relative path%s\n", path_to_check.c_str());
		#endif
	}
	return request_mkdir(path_to_check);

}

int capio_mkdirat(int dirfd, const char* pathname, mode_t mode) {
	#ifdef CAPIOLOG
	CAPIO_DBG("capio_mkdirat %s\n", pathname);
	#endif
	std::string path_to_check;
	if(is_absolute(pathname)) {
		path_to_check = pathname;
		#ifdef CAPIOLOG
		CAPIO_DBG("capio_mkdirat absolute %s\n", path_to_check.c_str());
		#endif
	}
	else {
		if(dirfd == AT_FDCWD) {
			path_to_check = create_absolute_path(pathname);
			if (path_to_check.length() == 0)
				return -2;
			#ifdef CAPIOLOG
			CAPIO_DBG("capio_mkdirat AT_FDCWD %s\n", path_to_check.c_str());
			#endif
		}
		else {
			if (is_directory(dirfd) != 1)
				return -2;
			std::string dir_path = get_dir_path(pathname, dirfd);
			if (dir_path.length() == 0)
				return -2;
			path_to_check = dir_path + "/" + pathname;
			#ifdef CAPIOLOG
			CAPIO_DBG("capio_mkdirat with dirfd %s\n", path_to_check.c_str());
			#endif
		}
	}
	return request_mkdir(path_to_check);
}


off64_t add_read_request(int fd, off64_t count, std::tuple<void*, off64_t*, off64_t*, off64_t*, int , int>& t) {
	std::string str = "read " + std::to_string(syscall(SYS_gettid)) + " " + std::to_string(fd) + " " + std::to_string(count);
	const char* c_str = str.c_str();
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
	//read response (offest)
	off64_t offset_upperbound;
	(*bufs_response)[syscall(SYS_gettid)]->read(&offset_upperbound);
	*std::get<3>(t) = offset_upperbound;
	off64_t file_shm_size = *std::get<2>(t);
	off64_t end_of_read = *std::get<1>(t) + count;
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
		*std::get<2>(t) = new_size;
	}
	return offset_upperbound;
}


void add_write_request(int fd, off64_t count) { //da modifcare con capio_file sia per una normale scrittura sia per quando si fa il batch
	char c_str[64];
	long int old_offset = *std::get<1>((*files)[fd]);
	*std::get<1>((*files)[fd]) += count; //works only if there is only one writer at time for each file
	if (actual_num_writes == num_writes_batch) {
		sprintf(c_str, "writ %ld %d %ld %ld", syscall(SYS_gettid),fd, old_offset, count);
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
	//sem_post((*sems_write)[syscall(SYS_gettid)]);
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
	auto it = files->find(fd);
	if (it != files->end()) {
		std::tuple<void*, off64_t*, off64_t*, off64_t*, int, int>* t = &(*files)[fd];
		off64_t* file_offset = std::get<1>(*t);
		if (whence == SEEK_SET) {
			if (offset >= 0) {
				*file_offset = offset;
				char c_str[64];
				sprintf(c_str, "seek %ld %d %zu", syscall(SYS_gettid),fd, *file_offset);
				buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
				off64_t offset_upperbound;
				(*bufs_response)[syscall(SYS_gettid)]->read(&offset_upperbound);
				*std::get<3>(*t) = offset_upperbound;
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
				sprintf(c_str, "seek %ld %d %zu", syscall(SYS_gettid),fd, *file_offset);
				buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
				off64_t offset_upperbound;
				(*bufs_response)[syscall(SYS_gettid)]->read(&offset_upperbound);
				*std::get<3>(*t) = offset_upperbound;
				return *file_offset;
			}
			else {
				errno = EINVAL;
				return -1;
			}
		}
		else if (whence == SEEK_END) {
			char c_str[64];
			off64_t file_size;
			sprintf(c_str, "send %ld %d", syscall(SYS_gettid),fd);
			buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
			(*bufs_response)[syscall(SYS_gettid)]->read(&file_size);
			off64_t offset_upperbound;
			offset_upperbound = file_size;
			*file_offset = file_size + offset;	
			*std::get<3>(*t) = offset_upperbound;
			return *file_offset;
		}
		else if (whence == SEEK_DATA) {
				char c_str[64];
				sprintf(c_str, "sdat %ld %d %zu", syscall(SYS_gettid),fd, *file_offset);
				buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
				off64_t offset_upperbound;
				(*bufs_response)[syscall(SYS_gettid)]->read(&offset_upperbound);
				*std::get<3>(*t) = offset_upperbound;
				(*bufs_response)[syscall(SYS_gettid)]->read(file_offset);
				return *file_offset;

		}
		else if (whence == SEEK_HOLE) {
				char c_str[64];
				sprintf(c_str, "shol %ld %d %zu", syscall(SYS_gettid),fd, *file_offset);
				buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
				off64_t offset_upperbound;
				(*bufs_response)[syscall(SYS_gettid)]->read(&offset_upperbound);
				*std::get<3>(*t) = offset_upperbound;
				(*bufs_response)[syscall(SYS_gettid)]->read(file_offset);
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
	#ifdef CAPIOLOG
	CAPIO_DBG("capio_openat %s\n", pathname);
	#endif
	std::string path_to_check;
	if(is_absolute(pathname)) {
		path_to_check = pathname;
		#ifdef CAPIOLOG
		CAPIO_DBG("capio_openat absolute %s\n", path_to_check.c_str());
		#endif
	}
	else {
		if(dirfd == AT_FDCWD) {
			path_to_check = create_absolute_path(pathname);
			if (path_to_check.length() == 0)
				return -2;
			#ifdef CAPIOLOG
			CAPIO_DBG("capio_openat AT_FDCWD %s\n", path_to_check.c_str());
			#endif
		}
		else {
			if (is_directory(dirfd) != 1)
				return -2;
			std::string dir_path = get_dir_path(pathname, dirfd);
			if (dir_path.length() == 0)
				return -2;
			path_to_check = dir_path + "/" + pathname;
			#ifdef CAPIOLOG
			CAPIO_DBG("capio_openat with dirfd %s\n", path_to_check.c_str());
			#endif
		}
	}
	auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());

	#ifdef CAPIOLOG
	CAPIO_DBG("CAPIO directory: %s\n", capio_dir->c_str());
	#endif
	if (res.first == capio_dir->end()) {
		if (capio_dir->size() == path_to_check.size()) {
			return -2;
			std::cerr << "ERROR: open to the capio_dir " << path_to_check << std::endl;
			exit(1);

		}
		else  {
		//create shm
			char shm_name[512];
			size_t i = 0;
			while (i < path_to_check.length()) {
				if (path_to_check[i] == '/' && i > 0)
					shm_name[i] = '_';
				else
					shm_name[i] = path_to_check[i];
				++i;
			}
			shm_name[i] = '\0';
			int fd;
			void* p = create_shm(shm_name, file_initial_size, &fd);
			add_open_request(path_to_check.c_str(), fd);
			off64_t* p_offset = (off64_t*) create_shm("offset_" + std::to_string(syscall(SYS_gettid)) + "_" + std::to_string(fd), sizeof(off64_t));
			*p_offset = 0;
			off64_t* init_size = new off64_t;
			*init_size = file_initial_size;
			off64_t* offset = new off64_t;
			*offset = 0;
			files->insert({fd, std::make_tuple(p, p_offset, init_size, offset, flags, 0)});
			(*capio_files_descriptors)[fd] = path_to_check;
			capio_files_paths->insert(path_to_check);
			if ((flags & O_APPEND) == O_APPEND) {
				capio_lseek(fd, 0, SEEK_END);
			}
			#ifdef CAPIOLOG
			CAPIO_DBG("capio_openat returning %d\n", fd);
			#endif
			return fd;
		}
	}
	else {
		return -2;
	}
}

ssize_t capio_write(int fd, const  void *buffer, size_t count) {
	auto it = files->find(fd);
	if (it != files->end()) {
		if (count > SSIZE_MAX) {
			std::cerr << "Capio does not support writes bigger then SSIZE_MAX yet" << std::endl;
			exit(1);
		}
		off64_t count_off = count;
		//bool in_shm = check_cache(fd);
		//if (in_shm) {
		off64_t file_shm_size = *std::get<2>((*files)[fd]);
		if (*std::get<1>((*files)[fd]) + count_off > file_shm_size) {
			off64_t file_size = *std::get<1>((*files)[fd]);
			off64_t new_size;
			if (file_size + count_off > file_shm_size * 2)
				new_size = file_size + count_off;
			else
				new_size = file_shm_size * 2;
			std::string shm_name = (*capio_files_descriptors)[fd];
			std::replace(shm_name.begin(), shm_name.end(), '/', '_');
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
		#ifdef CAPIOLOG
	CAPIO_DBG("capio_close %d %d\n", syscall(SYS_gettid), fd);
		#endif
	auto it = files->find(fd);
	if (it != files->end()) {
		add_close_request(fd);
		capio_files_descriptors->erase(fd);
		files->erase(fd);
		return close(fd);
	}
	else {
		#ifdef CAPIOLOG
		CAPIO_DBG("capio_close returning -2 %d %d\n", syscall(SYS_gettid), fd);
		#endif
		return -2;
	}
}

ssize_t capio_read(int fd, void *buffer, size_t count) {
		#ifdef CAPIOLOG
		CAPIO_DBG("capio_read %d %d %ld\n", syscall(SYS_gettid), fd, count);
		#endif
	auto it = files->find(fd);
	if (it != files->end()) {
		if (count >= SSIZE_MAX) {
			std::cerr << "capio does not support read bigger then SSIZE_MAX yet" << std::endl;
			exit(1);
		}
		off64_t count_off = count;
		std::tuple<void*, off64_t*, off64_t*, off64_t*, int, int>* t = &(*files)[fd];
		off64_t* offset = std::get<1>(*t);
		//bool in_shm = check_cache(fd);
		//if (in_shm) {
		off64_t bytes_read;
			if (*offset + count_off > *std::get<3>(*t)) {
				off64_t end_of_read;
				end_of_read = add_read_request(fd, count_off, *t);
				bytes_read = end_of_read - *offset;
				if (bytes_read > count_off)
					bytes_read = count_off;
			}
			else
				bytes_read = count_off;
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
	auto it = files->find(fd);
	if (it != files->end()) {
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
  auto it = files->find(fd);
  if (it != files->end()) {
	#ifdef CAPIOLOG
    CAPIO_DBG("capio_fcntl\n");
	#endif
    switch (cmd) {
    case F_GETFD: {
      return std::get<5>((*files)[fd]);
      break;
    }
    case F_SETFD: {
      std::get<5>((*files)[fd]) = arg;
      return 0;
      break;
    }
    case F_GETFL: {
      return std::get<4>((*files)[fd]);

      break;
    }
    case F_SETFL: {
      std::get<4>((*files)[fd]) = arg;
      return 0;
      break;
    }
    case F_DUPFD_CLOEXEC: {
      int dev_fd = open("/dev/null", O_RDONLY);
      if (dev_fd == -1)
        err_exit("open /dev/null");
      int res = fcntl(dev_fd, F_DUPFD_CLOEXEC, arg); //
      close(dev_fd);
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
	auto it = files->find(fd);
	if (it != files->end()) {
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
	auto it = files->find(fd);
	if (it != files->end()) {
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
	#ifdef CAPIOLOG
	CAPIO_DBG("capio ioctl %d %lu\n", fd, request);
	#endif
	auto it = files->find(fd);
	if (it != files->end()) {
		errno = ENOTTY;
		return -1;
	}
	else
		return -2;
	
}

/*
 * TODO: adding cleaning of shared memory
 * The process can never interact with the server
 * maybe because is a child process don't need to interact
 * with CAPIO
*/
void capio_exit_group(int status) {
	int pid = syscall(SYS_gettid);
	std::string str = "exig " + std::to_string(pid);
	const char* c_str = str.c_str();
	#ifdef CAPIOLOG
	CAPIO_DBG("capio exit group captured%d\n", pid);
	#endif
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
	#ifdef CAPIOLOG
	CAPIO_DBG("capio exit group terminated%d\n", pid);
	#endif
	return;
}



/*
 * Precondition: absolute_path must contain an absolute path
 *
 */

int capio_lstat(std::string absolute_path, struct stat* statbuf) {
	#ifdef CAPIOLOG
	CAPIO_DBG("capio_lstat %s\n", absolute_path.c_str());
	#endif
	auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), absolute_path.begin());
	if (res.first == capio_dir->end()) {
		if (capio_dir->size() == absolute_path.size()) {
			//it means capio_dir is equals to absolute_path
			return -2;

		}
	#ifdef CAPIOLOG
		CAPIO_DBG("capio_lstat sending msg to server\n");
	#endif
		std::string msg = "stat " + std::to_string(syscall(SYS_gettid)) + " " + absolute_path;
		const char* c_str = msg.c_str();
		buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
	#ifdef CAPIOLOG
		CAPIO_DBG("capio_lstat after sent msg to server\n");
	#endif
		off64_t file_size;
		off64_t is_dir;
		(*bufs_response)[syscall(SYS_gettid)]->read(&file_size); //TODO: these two reads don't work in multithreading
		if (file_size == -1) {
			errno = ENOENT;
			return -1;
		}
		(*bufs_response)[syscall(SYS_gettid)]->read(&is_dir);
		statbuf->st_dev = 100;

		
		std::hash<std::string> hash;		
		statbuf->st_ino = hash(absolute_path);

		if (is_dir == 0) {
	    	statbuf->st_mode = S_IFDIR | S_IRWXU | S_IWGRP | S_IRGRP | S_IXOTH| S_IROTH; // 0755 directory
			file_size = 4096;															
		}
																		
		else
			statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0644 regular file 
		statbuf->st_nlink = 1;
		statbuf->st_uid = getuid(); 
		statbuf->st_gid = getgid();
		statbuf->st_rdev = 0;
		statbuf->st_size = file_size;
	#ifdef CAPIOLOG
		CAPIO_DBG("lstat file_size=%ld\n",file_size);
	#endif
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
	#ifdef CAPIOLOG
	CAPIO_DBG("capio_lstat_wrapper\n");
	CAPIO_DBG("lstat  pathanem %s\n", path);
	#endif
	std::string absolute_path;	
	absolute_path = create_absolute_path(path);
	if (absolute_path.length() == 0)
		return -2;
	return capio_lstat(absolute_path, statbuf);	
}

int capio_fstat(int fd, struct stat* statbuf) {
	auto it = files->find(fd);
	if (it != files->end()) {
	#ifdef CAPIOLOG
		CAPIO_DBG("capio_fstat captured\n");
	#endif
		std::string msg = "fsta " + std::to_string(syscall(SYS_gettid)) + " " + std::to_string(fd);
		buf_requests->write(msg.c_str(), (strlen(msg.c_str()) + 1) * sizeof(char));
	#ifdef CAPIOLOG
		CAPIO_DBG("capio_fstat captured after write\n");
	#endif
		off64_t file_size;
		off64_t is_dir;
		(*bufs_response)[syscall(SYS_gettid)]->read(&file_size);
		(*bufs_response)[syscall(SYS_gettid)]->read(&is_dir);
		statbuf->st_dev = 100;

		std::hash<std::string> hash;		
		statbuf->st_ino = hash((*capio_files_descriptors)[fd]);
		
		if (is_dir == 0)
	    	statbuf->st_mode = S_IFDIR | S_IRWXU | S_IWGRP | S_IRGRP | S_IXOTH| S_IROTH; // 0755 directory
																		
		else
	    	statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0666 regular file 
		statbuf->st_nlink = 1;
		statbuf->st_uid = getuid();
		statbuf->st_gid = getgid();
		statbuf->st_rdev = 0;
		statbuf->st_size = file_size;
	#ifdef CAPIOLOG
		CAPIO_DBG("capio_fstat file_size=%ld\n", file_size);
	#endif
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
	CAPIO_DBG("fstatat pathanem %s\n", pathname);
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
			res = capio_lstat(path, statbuf);
		}
	}
	else { //dirfd is ignored
		res = capio_lstat(std::string(pathname), statbuf);
	}
	return res;

}


int capio_creat(const char* pathname, mode_t mode) {
	#ifdef CAPIOLOG
	CAPIO_DBG("capio_creat %s\n", pathname);
	#endif
	int res = capio_openat(AT_FDCWD, pathname, O_CREAT | O_WRONLY | O_TRUNC);

	#ifdef CAPIOLOG
	CAPIO_DBG("capio_creat %s returning %d\n", pathname, res);
	#endif
	return res;
}

int is_capio_file(std::string abs_path) {
	auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), abs_path.begin());
	if (it.first == capio_dir->end()) 
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
	std::string msg = "accs " + std::string(path);
	off64_t res;
	buf_requests->write(msg.c_str(), (strlen(msg.c_str()) + 1) * sizeof(char));
	(*bufs_response)[syscall(SYS_gettid)]->read(&res);
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
			auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), path.begin());
			if (it.first == capio_dir->end()) {
				if (capio_dir->size() == path.size()) {
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
	auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), abs_path.begin());
	if (it.first == capio_dir->end()) {
		if (capio_dir->size() == abs_path.size()) {
			std::cerr << "ERROR: unlink to the capio_dir " << abs_path << std::endl;
			exit(1);
		}
		int pid = syscall(SYS_gettid);
		std::string msg = "unlk " + std::to_string(pid) + " " + std::string(abs_path);
		buf_requests->write(msg.c_str(), (strlen(msg.c_str()) + 1) * sizeof(char));
		off64_t res_unlink;
	    (*bufs_response)[syscall(SYS_gettid)]->read(&res_unlink); 	
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
	#ifdef CAPIOLOG
	CAPIO_DBG("capio_unlink %s\n", pathname);
	#endif
	if (capio_dir == nullptr) {
		//unlink can be called before initialization (see initialize_from_snapshot)
		return -2;
	}
	std::string abs_path = create_absolute_path(pathname);
	if (abs_path.length() == 0)
		return -2;
	int res = capio_unlink_abs(abs_path);
	return res;

}

int capio_unlinkat(int dirfd, const char* pathname, int flags) {
	#ifdef CAPIOLOG
	CAPIO_DBG("capio_unlinkat\n");
	#endif
	
	if (capio_dir->length() == 0) {
		//unlinkat can be called before initialization (see initialize_from_snapshot)
		return -2;
	}
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
	#ifdef CAPIOLOG
			CAPIO_DBG("capio_unlinkat path=%s\n",path.c_str());
	#endif
			
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

void add_dup_request(int old_fd, int new_fd) {
	std::string msg = "dupp " + std::to_string(syscall(SYS_gettid)) + " " + std::to_string(old_fd) + " " + std::to_string(new_fd);
	const char* c_str = msg.c_str();
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
}

int capio_dup(int fd) {
	int res;
	auto it = files->find(fd);
	#ifdef CAPIOLOG
	CAPIO_DBG("capio_dup\n");
	#endif
	if (it != files->end()) {
	#ifdef CAPIOLOG
		CAPIO_DBG("handling capio_dup\n");
	#endif
		res = open("/dev/null", O_WRONLY);
		if (res == -1)
			err_exit("open in capio_dup");
		add_dup_request(fd, res);
		(*files)[res] = (*files)[fd];
		(*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];
	#ifdef CAPIOLOG
		CAPIO_DBG("handling capio_dup returning res %d\n", res);
	#endif
	}
	else
		res = -2;
	return res;
}

int capio_dup2(int fd, int fd2) {
	int res;
	auto it = files->find(fd);
	#ifdef CAPIOLOG
	CAPIO_DBG("capio_dup 2\n");
	#endif
	if (it != files->end()) {
	#ifdef CAPIOLOG
		CAPIO_DBG("handling capio_dup2\n");
	#endif
		dup2_enabled = false;
		res = dup2(fd, fd2);
		dup2_enabled = true;
		if (res == -1)
			return -1;
		add_dup_request(fd, res);
		(*files)[res] = (*files)[fd];
		(*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];
	#ifdef CAPIOLOG
		CAPIO_DBG("handling capio_dup returning res %d\n", res);
	#endif
	}
	else
		res = -2;
	return res;
}

void copy_parent_files() {
	#ifdef CAPIOLOG
	CAPIO_DBG("Im process %d and my parent is %d\n", syscall(SYS_gettid), parent_tid);
	#endif
	std::string msg = "clon " + std::to_string(parent_tid) + " " + std::to_string(syscall(SYS_gettid));
	buf_requests->write(msg.c_str(), (strlen(msg.c_str()) + 1) * sizeof(char));
}

pid_t capio_fork() {
	fork_enabled = false;
	#ifdef CAPIOLOG
	CAPIO_DBG("fork captured\n");
	#endif
	pid_t pid = fork();
	if (pid == 0) { //child
		parent_tid = syscall(SYS_gettid); //now syscall(SYS_gettid) is the copy of the father
		mtrace_init();
		copy_parent_files();
		return 0;
	}
	fork_enabled = true;
	return pid;

}
/*
 * From "The Linux Programming Interface: A Linux and Unix System Programming Handbook", by Micheal Kerrisk:
 * "Within the kernel, fork(), vfork(), and clone() are ultimately
 * implemented by the same function (do_fork() in kernel/fork.c). 
 * At this level, cloning is much closer to forking: sys_clone() doesn’t 
 * have the func and func_arg arguments, and after the call, sys_clone() 
 * returns in the child in the same manner as fork(). The main text 
 * describes the clone() wrapper function that glibc provides for sys_clone().
 * This wrapper function invokes func after sys_clone() returns in the child."
*/
pid_t capio_clone(int flags, void* child_stack, void* parent_tidpr, void* tls, void* child_tidpr) {
	//clone_sl.lock();
	sem_wait(sem_clone);
	#ifdef CAPIOLOG
	CAPIO_DBG("clone captured flags %d\n", flags);
	#endif
	pid_t pid;
	
	if ((flags & CLONE_THREAD) == CLONE_THREAD) {//thread creation
	#ifdef CAPIOLOG
	CAPIO_DBG("thread creation\n");
	#endif
		pid = 1;
		//long int* p = (long int*) create_shm("capio_clone_parent_" + std::to_string(syscall(SYS_gettid)), sizeof(long int));
		//*p = syscall(SYS_gettid);
		parent_tid = syscall(SYS_gettid);
		thread_created = true;
		if (sem_family == nullptr)
			sem_family = sem_open(("capio_sem_family_" + std::to_string(syscall(SYS_gettid))).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	#ifdef CAPIOLOG
			CAPIO_DBG("sem_family %ld\n", sem_family);
	CAPIO_DBG("thread creation ending\n");
	#endif
		//sl->lock();
	(*stat_enabled)[parent_tid] = false;
	#ifdef CAPIOLOG
		CAPIO_DBG("first call size %ld\n", first_call->size());
#endif
		sem_wait(sem_first_call);
		first_call->erase(syscall(SYS_gettid));
		sem_post(sem_first_call);
	#ifdef CAPIOLOG
		CAPIO_DBG("first call after size %ld\n", first_call->size());
#endif
	#ifdef CAPIOLOG
	CAPIO_DBG("thread creation ending 2\n");
	(*stat_enabled)[parent_tid] = true;
#endif
		//sl->unlock();
	}
	else { //process creation
#ifdef CAPIOLOG
	CAPIO_DBG("process creation\n");
	#endif
	fork_enabled = false;
	parent_tid = syscall(SYS_gettid); //now syscall(SYS_gettid) is the copy of the father
	pid = fork();
	if (pid == 0) { //child
		mtrace_init();
		copy_parent_files();
		if (sem_family == nullptr)
			sem_family = sem_open(("capio_sem_family_" + std::to_string(parent_tid)).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);

		sem_post(sem_family);
	#ifdef CAPIOLOG
		CAPIO_DBG("returning from clone\n");
	#endif
		fork_enabled = true;
		return 0;
	}
	#ifdef CAPIOLOG
		CAPIO_DBG("father before wait\n");
	#endif
		if (sem_family == nullptr)
			sem_family = sem_open(("capio_sem_family_" + std::to_string(syscall(SYS_gettid))).c_str(),  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
		sem_wait(sem_family);
	#ifdef CAPIOLOG
		CAPIO_DBG("father returning from clone\n");
	#endif
		fork_enabled = true;

	}
	return pid;
}

off64_t add_getdents_request(int fd, off64_t count, std::tuple<void*, off64_t*, off64_t*, off64_t*, int , int>& t) {
	std::string str = "dent " + std::to_string(syscall(SYS_gettid)) + " " + std::to_string(fd) + " " + std::to_string(count);
	const char* c_str = str.c_str();
	buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
	//read response (offest)
	off64_t offset_upperbound;
	(*bufs_response)[syscall(SYS_gettid)]->read(&offset_upperbound);
	*std::get<3>(t) = offset_upperbound;
	off64_t file_shm_size = *std::get<2>(t);
	off64_t end_of_read = *std::get<1>(t) + count;
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
		*std::get<2>(t) = new_size;
	}
	return offset_upperbound;
}

off64_t round(off64_t bytes) {
	off64_t res = 0;
	off64_t ld_size = sizeof(struct linux_dirent);
	while (res + ld_size <= bytes)
		res += ld_size;
	return res;
}

//TODO: too similar to capio_read, refactoring needed

ssize_t capio_getdents(int fd, void *buffer, size_t count) {
		#ifdef CAPIOLOG
		CAPIO_DBG("capio_getdents %d %d %ld\n", syscall(SYS_gettid), fd, count);
		#endif
	auto it = files->find(fd);
	if (it != files->end()) {
		if (count >= SSIZE_MAX) {
			std::cerr << "capio does not support read bigger then SSIZE_MAX yet" << std::endl;
			exit(1);
		}
		off64_t count_off = count;
		std::tuple<void*, off64_t*, off64_t*, off64_t*, int, int>* t = &(*files)[fd];
		off64_t* offset = std::get<1>(*t);
		//bool in_shm = check_cache(fd);
		//if (in_shm) {
		off64_t bytes_read;
			if (*offset + count_off > *std::get<3>(*t)) {
				off64_t end_of_read;
				end_of_read = add_getdents_request(fd, count_off, *t);
				bytes_read = end_of_read - *offset;
				if (bytes_read > count_off)
					bytes_read = count_off;
			}
			else
				bytes_read = count_off;
			bytes_read = round(bytes_read);
			read_shm(std::get<0>(*t), *offset, buffer, bytes_read);
		//}
		//else {
			//read_from_disk(fd, offset, buffer, count);
		//}
		*offset = *offset + bytes_read;
		#ifdef CAPIOLOG
		CAPIO_DBG("capio_getdents returning %ld\n", bytes_read);
		#endif
		return bytes_read;
	}
	else { 
		return -2;
	}
}


static int hook(long syscall_number, long arg0, long arg1, long arg2, long arg3,
                long arg4, long arg5, long *result) {
	
	if (syscall_number == SYS_gettid)
		return 1;
	if (syscall_number == SYS_mmap)
		return 1;
	if (syscall_number == SYS_mprotect)
		return 1;
	if (syscall_number == SYS_munmap)
		return 1;
	if (syscall_number == SYS_futex)
		return 1;
	
	long int my_tid = syscall(SYS_gettid);
	if (stat_enabled == nullptr) {
		stat_enabled = new std::unordered_map<long int, bool>;
	}
	auto it = stat_enabled->find(my_tid);
	if (it != stat_enabled->end()) {
		if (!(it->second))
			return 1;
		}

	if (sem_first_call == nullptr) {
		sem_first_call = new sem_t;
		sem_init(sem_first_call, 0, 1);
		sem_clone = new sem_t;
		sem_init(sem_clone, 0, 1);
	}
	
	sem_wait(sem_first_call);
	

	  if (first_call == nullptr || first_call->find(syscall(SYS_gettid)) == first_call->end()) {
		mtrace_init();
	  }
	else {
	    sem_post(sem_first_call);
	  }
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
	(*stat_enabled)[my_tid] = false;
	#ifdef CAPIOLOG
   	CAPIO_DBG("write captured %d %d\n", syscall(SYS_gettid), fd);
	#endif
	#ifdef CAPIOLOG
    CAPIO_DBG("files size %d %d\n",syscall(SYS_gettid),files->size());
    CAPIO_DBG("capio_files_paths size %d %d\n",syscall(SYS_gettid),capio_files_paths->size());
	#endif
		
	(*stat_enabled)[my_tid] = true;
    
    ssize_t res = capio_write(fd, buf, count);
	//#ifdef CAPIOLOG
    //CAPIO_DBG("write returning %ld \n", res);
	//#endif
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
	#ifdef CAPIOLOG
    CAPIO_DBG("seek %d %d\n", syscall(SYS_gettid), fd);
	#endif
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
	#ifdef CAPIOLOG
    CAPIO_DBG("capio_*xattr\n");
	#endif
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
      res = capio_lstat_wrapper(path, buf);
      if (res != -2) {
        *result = (res < 0 ? -errno : res);
        hook_ret_value = 0;
      }
    break;
  }
  case SYS_stat: {
    const char *path = reinterpret_cast<const char *>(arg0);
    struct stat *buf = reinterpret_cast<struct stat *>(arg1);
      res = capio_lstat_wrapper(path, buf);
      if (res != -2) {
        *result = (res < 0 ? -errno : res);
        hook_ret_value = 0;
      }
    break;
  }

  case SYS_fstat: {
    int fd = arg0;
    struct stat *buf = reinterpret_cast<struct stat *>(arg1);
#ifdef  CAPIOLOG
	CAPIO_DBG("fstat %d %ld\n", fd, syscall(SYS_gettid));
#endif
      res = capio_fstat(fd, buf);
      if (res != -2) {
        *result = (res < 0 ? -errno : res);
        hook_ret_value = 0;
      }
    break;
  }

  case SYS_newfstatat: {
    int dirfd = arg0;
    const char *pathname = reinterpret_cast<const char *>(arg1);
    struct stat *statbuf = reinterpret_cast<struct stat *>(arg2);
    int flags = arg3;
      res = capio_fstatat(dirfd, pathname, statbuf, flags);
      if (res != -2) {
        *result = (res < 0 ? -errno : res);
        hook_ret_value = 0;
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
	#ifdef CAPIOLOG
    CAPIO_DBG("fork before captured\n");
	#endif
    if (fork_enabled) {
      res = capio_fork();
      *result = (res < 0 ? -errno : res);
      hook_ret_value = 0;
    } else
      res = -2;
    break;
  }

  case SYS_clone: {

    int flags = arg0;
 	void* child_stack = reinterpret_cast<void*>(arg1);
	 void* parent_tidpr = reinterpret_cast<void*>(arg2);
	 void* tls = reinterpret_cast<void*>(arg3);
	 void* child_tidpr = reinterpret_cast<void*>(arg4);
    if (fork_enabled) {
      res = capio_clone(flags, child_stack, parent_tidpr, tls, child_tidpr);
      if (res == 1)
	      return 1;
	  if (res != -2) {
      	*result = (res < 0 ? -errno : res);
      	hook_ret_value = 0;
	  }
    } 
    break;
  }

	case SYS_mkdir: {
		const char* pathname = reinterpret_cast<const char*>(arg0);
		mode_t mode = arg1;
		res = capio_mkdir(pathname, mode);
    	if (res != -2) {
      		*result = (res < 0 ? -errno : res);
      		hook_ret_value = 0;
    	}
		break;
	}

	case SYS_mkdirat: {
		int dirfd = arg0;
		const char* pathname = reinterpret_cast<const char*>(arg1);
		mode_t mode = arg2;
		res = capio_mkdirat(dirfd, pathname, mode);
    	if (res != -2) {
      		*result = (res < 0 ? -errno : res);
      		hook_ret_value = 0;
    	}
		break;
	}
	
	case SYS_execve: {
		crate_snapshot();
		break;
	}
	
	case SYS_getdents: {
		int fd =  arg0;
		struct linux_dirent* dirp = reinterpret_cast<struct linux_dirent*>(arg1);
		unsigned int count = arg2;
		ssize_t res = capio_getdents(fd, dirp, count);				
    	if (res != -2) {
      		*result = (res < 0 ? -errno : res);
      		hook_ret_value = 0;
    	}
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

