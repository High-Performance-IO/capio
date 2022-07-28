#include "circular_buffer.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <set>
#include <string>
#include <iostream>
#include <filesystem>
#include <climits>

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

static bool is_fstat = true;
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

std::unordered_map<int, std::tuple<void*, off64_t*, off64_t, off64_t, Capio_file, int, int>> files;
Circular_buffer<char>* buf_requests;
 
Circular_buffer<off_t>* buf_response;
sem_t* sem_response;
sem_t* sem_write;
int* client_caching_info;
int* caching_info_size;

// fd -> pathname
std::unordered_map<int, std::string> capio_files_descriptors; 
std::unordered_set<std::string> capio_files_paths;

std::unordered_map<int, std::pair<std::vector<int>, bool>> fd_copies;

int is_directory(const char *path) {
   struct stat statbuf;
   if (stat(path, &statbuf) != 0) {
		std::cerr << "error: stat" << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
		exit(1);
   }
   return S_ISDIR(statbuf.st_mode);
}

static bool first_call = true;

/*
 * This function must be called only once
 *
 */

void mtrace_init(void) {
	char* val;
	first_call = false;
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
	buf_requests = new Circular_buffer<char>("circular_buffer", 256L * 1024 * 1024, sizeof(char) * 600);
	buf_response = new Circular_buffer<off_t>("buf_response" + std::to_string(getpid()), 256L * 1024 * 1024, sizeof(off_t));
	client_caching_info = (int*) create_shm("caching_info" + std::to_string(getpid()), 4096);
	caching_info_size = (int*) create_shm("caching_info_size" + std::to_string(getpid()), sizeof(int));
	*caching_info_size = 0; 



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

off64_t add_read_request(int fd, off64_t count, std::tuple<void*, off64_t*, off64_t, off64_t, Capio_file, int , int>& t) {
	std::string str = "read " + std::to_string(getpid()) + " " + std::to_string(fd) + " " + std::to_string(count);
	const char* c_str = str.c_str();
	buf_requests->write(c_str);
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
	long int old_offset = *std::get<1>(files[fd]);
	*std::get<1>(files[fd]) += count; //works only if there is only one writer at time for each file
	if (actual_num_writes == num_writes_batch) {
		sprintf(c_str, "writ %d %d %ld %ld", getpid(),fd, old_offset, count);
		std::get<4>(files[fd]).insert_sector(old_offset, old_offset + count);
		buf_requests->write(c_str);
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
	auto it = capio_files_descriptors.find(fd);
	if (it == capio_files_descriptors.end()) {
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
	auto it = capio_files_descriptors.find(fd);
	if (it == capio_files_descriptors.end()) {
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

int capio_openat(int dirfd, const char* pathname, int flags) {
	std::cout << "capio_openat " << pathname << std::endl;
	if (first_call)
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
			try{
			path_to_check = std::filesystem::canonical(path_to_check);
			}
			catch (const std::exception& ex) {
				//TODO: if O_CREAT is not a probelm beacuse it's ok if the file still doesn't exist
				std::cout << "exception canonical " << path_to_check << std::endl; 
			}
		}
		else {
			std::string curr_dir = std::filesystem::current_path();
			path_to_check = curr_dir + "/" + pathname;
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
			std::cout << "creating shmm" << std::endl;
			printf("%s\n", shm_name);
			int fd;
			void* p = create_shm(shm_name, 1024L * 1024 * 1024* 2, &fd);
			add_open_request(shm_name, fd);
			off64_t* p_offset = create_shm_off64_t("offset_" + std::to_string(getpid()) + "_" + std::to_string(fd));
			*p_offset = 0;
			files[fd] = std::make_tuple(p, p_offset, file_initial_size, 0, Capio_file(), flags, 0);
			capio_files_descriptors[fd] = shm_name;
			fd_copies[fd] = std::make_pair(std::vector<int>(), true);
			capio_files_paths.insert(pathname);
			return fd;
		}
	}
	else {
		return -2;
	}
}

ssize_t capio_write(int fd, const  void *buffer, size_t count) {
	auto it = fd_copies.find(fd);
	if (it != fd_copies.end()) {
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
		off64_t file_shm_size = std::get<2>(files[fd]);
		if (*std::get<1>(files[fd]) + count_off > file_shm_size) {
			off64_t file_size = *std::get<1>(files[fd]);
			off64_t new_size;
			if (file_size + count_off > file_shm_size * 2)
				new_size = file_size + count_off;
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
		write_shm(std::get<0>(files[fd]), *std::get<1>(files[fd]), buffer, count_off);
		add_write_request(fd, count_off); //bottleneck
		//}
		//else {
			//write_to_disk(fd, files[fd].second, buffer, count);
		//}
		return count;
	}
	else {
		return -2;	
	}

}

int capio_close(int fd) {
	if (first_call)
		mtrace_init();
	auto it = fd_copies.find(fd);
	if (it != fd_copies.end()) {
		if (! it->second.second) {
			fd = it->second.first[0];
		}
		add_close_request(fd);
		auto& copies = fd_copies[fd].first;
		if (copies.size() > 0) {
			int master_fd = copies[0];
			files[master_fd] = files[fd];
			capio_files_descriptors[master_fd] = capio_files_descriptors[fd];
			fd_copies[master_fd].second = true;
			auto& master_copies = fd_copies[master_fd].first;
			master_copies.erase(master_copies.begin());
			for (size_t i = 1; i < copies.size(); ++i) {
				int fd_copy = copies[i];
				master_copies.push_back(fd_copy);
				auto& copy_vec = fd_copies[fd_copy].first;
				copy_vec.erase(copy_vec.begin());
				copy_vec.push_back(master_fd);
			}

		}
		capio_files_descriptors.erase(fd);
		fd_copies.erase(fd);
		files.erase(fd);
		return close(fd);
	}
	else {
		return -2;
	}
}

ssize_t capio_read(int fd, void *buffer, size_t count) {
	auto it = fd_copies.find(fd);
	if (it != fd_copies.end()) {
		if (! it->second.second) {
			fd = it->second.first[0];
		}
		if (count >= SSIZE_MAX) {
			std::cerr << "capio does not support read bigger then SSIZE_MAX yet" << std::endl;
			exit(1);
		}
		off64_t count_off = count;
		std::tuple<void*, off64_t*, off64_t, off64_t, Capio_file, int, int>* t = &files[fd];
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
			//std::cout << "after read shm " << bytes_read << std::endl;
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
	auto it = fd_copies.find(fd);
	if (it != fd_copies.end()) {
		if (! it->second.second) {
			fd = it->second.first[0];
		}
		std::tuple<void*, off64_t*, off64_t, off64_t, Capio_file, int, int>* t = &files[fd];
		off64_t* file_offset = std::get<1>(*t);
		if (whence == SEEK_SET) {
			if (offset >= 0) {
				*file_offset = offset;
				char c_str[64];
				sprintf(c_str, "seek %d %d %zu", getpid(),fd, *file_offset);
				buf_requests->write(c_str);
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
				buf_requests->write(c_str);
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
			std::cerr << "SEEK_END not supported yet" << std::endl;
			exit(1);
		}
		else if (whence == SEEK_DATA) {
				char c_str[64];
				sprintf(c_str, "sdat %d %d %zu", getpid(),fd, *file_offset);
				buf_requests->write(c_str);
				off64_t offset_upperbound;
				buf_response->read(&offset_upperbound);
				std::get<3>(*t) = offset_upperbound;
				buf_response->read(file_offset);
				return *file_offset;

		}
		else if (whence == SEEK_HOLE) {
				char c_str[64];
				sprintf(c_str, "shol %d %d %zu", getpid(),fd, *file_offset);
				buf_requests->write(c_str);
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

ssize_t capio_writev(int fd, const struct iovec* iov, int iovcnt) {
	auto it = fd_copies.find(fd);
	if (it != fd_copies.end()) {
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
	auto it = fd_copies.find(fd);
	if (it != fd_copies.end()) {
		if (! it->second.second) {
			fd = it->second.first[0];
		}
		switch (cmd) {
			case F_GETFD: {
				return std::get<6>(files[fd]);
			break;
			}
			case F_SETFD: {
				std::get<6>(files[fd]) = arg;
				return 0;
			break;
			}
			case F_GETFL: {
				return std::get<5>(files[fd]);

			break;
			}
			case F_SETFL: {
				std::get<5>(files[fd]) = arg;
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
					fd_copies[fd].first.push_back(res);
					fd_copies[res].first.push_back(fd);
					fd_copies[res].second = false;
				}
				return res;
			break;
			}
			default:
				std::cerr << "fcntl with cmd " << cmd << " is not yet supported" << std::endl;
				exit(1);
		}
	}
	else 
		return -2;
}
ssize_t capio_fgetxattr(int fd, const char* name, void* value, size_t size) {
	auto it = fd_copies.find(fd);
	if (it != fd_copies.end()) {
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
	auto it = fd_copies.find(fd);
	if (it != fd_copies.end()) {
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
	auto it = fd_copies.find(fd);
	if (it != fd_copies.end()) {
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
	buf_requests->write(c_str);
	return;
}

static int
hook(long syscall_number,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5,
			long *result)
{
	int hook_ret_value = 1;
	int res = 0;
	switch (syscall_number) {

		case SYS_open:
			hook_ret_value = 1;
			break;

		case SYS_openat: {
			int dirfd = static_cast<int>(arg0);
			const char* pathname = reinterpret_cast<const char*>(arg1);
			int flags = static_cast<int>(arg2);
			int res = capio_openat(dirfd, pathname, flags);
			if (res != -2) {
				*result = res;
				hook_ret_value = 0;
			}
			break;
		}

		case SYS_write: {
			int fd = static_cast<int>(arg0);
			const void* buf = reinterpret_cast<const void*>(arg1);
			size_t count = static_cast<size_t>(arg2);
			ssize_t res = capio_write(fd, buf, count);
			if (res != -2) {
				*result = res;
				hook_ret_value = 0;
			}
			break;
		}

		case SYS_read: {
			int fd = static_cast<int>(arg0);
			void* buf = reinterpret_cast<void*>(arg1);
			size_t count = static_cast<size_t>(arg2);
			ssize_t res = capio_read(fd, buf, count);
			if (res != -2) {
				*result = res;
				hook_ret_value = 0;
			}
			break;
		}

		case SYS_close: {
			int fd = static_cast<int>(arg0);
			int res = capio_close(fd);
			if (res != -2) {
				*result = res;
				hook_ret_value = 0;
			}
			break;
		}

		case SYS_lseek: {
			int fd = static_cast<int>(arg0);
			off_t offset = static_cast<off_t>(arg1);
			int whence = static_cast<int>(arg2);
			off_t res = capio_lseek(fd, offset, whence);
			if (res != -2) {
				*result = res;
				hook_ret_value = 0;
			}
			break;
		}

		case SYS_writev: {
			int fd = static_cast<int>(arg0);
			const struct iovec* iov = reinterpret_cast<const struct iovec*>(arg1);
			int iovcnt = static_cast<int>(arg2);
			ssize_t res = capio_writev(fd, iov, iovcnt);
			if (res != -2) {
				*result = res;
				hook_ret_value = 0;
			}
			break;
		}

		case SYS_fcntl : {
			int fd = arg0;		
			int cmd = arg1;
			int arg = arg2;
			int res = capio_fcntl(fd, cmd, arg);
			if (res != -2) {
				*result = res;
				hook_ret_value = 0;
			}

			break;
		}

		case SYS_fgetxattr: {
			int fd = arg0;
			const char* name = reinterpret_cast<const char*>(arg1);
			void* value = reinterpret_cast<void*>(arg2);
			size_t size = arg3;
			ssize_t res = capio_fgetxattr(fd, name, value, size);
			if (res != -2) {
				*result = res;
				hook_ret_value = 0;
			}
			break;
		}
		
		case SYS_flistxattr: {
			int fd = arg0;
			char* list = reinterpret_cast<char*>(arg1);
			size_t size = arg2;
			ssize_t res = capio_flistxattr(fd, list, size);
			if (res != -2) {
				*result = res;
				hook_ret_value = 0;
			}
			break;
		}

		case SYS_ioctl: {
			int fd = arg0;
			unsigned long request = arg1;
			int res = capio_ioctl(fd, request);
			if (res != -2) {
				*result = res;
				hook_ret_value = 0;
			}
			break;
		}

		case SYS_exit_group: {
			int status = arg0;
			capio_exit_group(status);
			hook_ret_value = 0;
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

/*
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



DIR* opendir(const char* name) {
	return real_opendir(name);
}

struct dirent* readdir(DIR* dirp) { 
	return real_readdir(dirp);
}

*/
