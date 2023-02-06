#include "circular_buffer.hpp"

#include <string>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <tuple>
#include <iostream>
#include <pthread.h>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <fstream>

#include <linux/limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <dirent.h> /* Defines DT_* constants */

#include <mpi.h>

#include "capio_file.hpp"
#include "spsc_queue.hpp"
#include "simdjson.h"
#include "utils/common.hpp"


#define DNAME_LENGTH 128
#define N_ELEMS_DATA_BUFS 10 
//256KB
#define WINDOW_DATA_BUFS 262144

using namespace simdjson;

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

std::ofstream logfile;

const static int theoretical_size = sizeof(unsigned long) + sizeof(off_t) + sizeof(unsigned short) + sizeof(char) * DNAME_LENGTH + 2;

std::string* capio_dir = nullptr;

const long int max_shm_size = 1024L * 1024 * 1024 * 16;

// maximum size of shm for each file
const long int max_shm_size_file = 1024L * 1024 * 1024 * 8;

// initial size for each file (can be overwritten by the user)
const size_t file_initial_size = 1024L * 1024 * 1024 * 4;

const size_t dir_initial_size = 1024L * 1024 * 1024;

bool shm_full = false;
long int total_bytes_shm = 0;

off64_t PREFETCH_DATA_SIZE = 0;

MPI_Request req;

/*
 * For multithreading:
 * tid -> pid
 */

std::unordered_map<int, int> pids;

// tid -> fd ->(file_data, index)
std::unordered_map<int, std::unordered_map<int, std::tuple<void*, off64_t*>>> processes_files;

// tid -> fd -> pathname
std::unordered_map<int, std::unordered_map<int, std::string>> processes_files_metadata;

// tid -> (response shared buffer, index)
std::unordered_map<int, Circular_buffer<off_t>*> response_buffers;

// tid -> (client_to_server_data_buf, server_to_client_data_buf)
std::unordered_map<int, std::pair<SPSC_queue<char>*, SPSC_queue<char>*>> data_buffers;

/*
 * Regarding the map caching info.
 * The pointer int* points to a buffer in shared memory.
 * This buffer is composed by pairs. The first element of the pair is a file descriptor.
 * The second element can be 0 (in case the file is in shared memory) or 1 (in case
 * the file is in the disk). This information will be used by the client when it performs
 * a read or a write into a file in order to know where to read/write the data.
 *
 * Another alternative would be to have only one caching_info buffer for all the
 * processes of an application. Neither solution is better for all the possible cases.
 *
 */

// tid -> (response shared buffer, size)
//std::unordered_map<int, std::pair<int*, int*>> caching_info;

/* pathname -> (file_data, file_size, mapped_shm_size, first_write, capio_file, real_file size)
 * file_size is the number of bytes stored in the local node
 * the real_size is the file size (that belongs to another node)
 * If file_size < real_file_size it means that only part of the file
 * is stored in the node.
 * The mapped shm size isn't the the size of the file shm
 * but it's the mapped shm size in the virtual adress space
 * of the server process. The effective size can ge greater
 * in a given moment.
 */
std::unordered_map<std::string, std::tuple<void*, off64_t*, off64_t, bool, Capio_file, off64_t>> files_metadata;

std::unordered_map<std::string, std::tuple<std::string, std::string>> metadata_conf;

/*
 * pid -> pathname -> bool
 * Different thread with the same pid are threated as a single writer
 */

std::unordered_map<int, std::unordered_map<std::string, bool>> writers;

// pathname -> node
std::unordered_map<std::string, char*> files_location;

// node -> rank
std::unordered_map<std::string, int> nodes_helper_rank;

/*
 *
 * It contains all the reads requested by local processes to read files that are in the local node for which the data is not yet avaiable.
 * path -> [(tid, fd, numbytes), ...]
 *
 */

std::unordered_map<std::string, std::vector<std::tuple<int, int, off64_t>>>  pending_reads;

/*
 *
 * It contains all the reads requested to the remote nodes that are not yet satisfied 
 * path -> [(tid, fd, numbytes), ...]
 *
 */

std::unordered_map<std::string, std::list<std::tuple<int, int, off64_t>>>  my_remote_pending_reads;

/*
 *
 * It contains all the stats requested to the remote nodes that are not yet satisfied 
 * path -> [tid1, tid2, tid3, ...]
 *
 */

std::unordered_map<std::string, std::list<int>>  my_remote_pending_stats;
/*
 *
 * It contains all the read requested by other nodes for which the data is not yet avaiable 
 * path -> [(offset, numbytes, sem_pointer), ...]
 *
 */

std::unordered_map<std::string, std::list<std::tuple<size_t, size_t, sem_t*>>> clients_remote_pending_reads;

std::unordered_map<std::string, std::list<sem_t*>> clients_remote_pending_stat;

// it contains the file saved on disk
std::unordered_set<std::string> on_disk;


//name of the node

char node_name[MPI_MAX_PROCESSOR_NAME];

Circular_buffer<char>* buf_requests; 
//std::unordered_map<int, sem_t*> sems_response;
std::unordered_map<int, sem_t*>* sems_write;

sem_t internal_server_sem;
sem_t remote_read_sem;
sem_t handle_remote_read_sem;
sem_t handle_remote_stat_sem;
sem_t handle_local_read_sem;

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
	//free all the memory used
	for (auto& pair : files_metadata) {
		shm_unlink(pair.first.c_str());
		shm_unlink((pair.first + "_size").c_str());
	}
	for (auto& p1 : processes_files) {
		for(auto& p2 : p1.second) {
			shm_unlink(("offset_" + std::to_string(p1.first) +  "_" + std::to_string(p2.first)).c_str());
		}
	}
	for (auto& pair : response_buffers) {
		pair.second->free_shm();
		delete pair.second;
//		sem_unlink(("sem_response_read" + std::to_string(pair.first)).c_str());
		sem_unlink(("sem_response_write" + std::to_string(pair.first)).c_str());
		sem_unlink(("sem_write" + std::to_string(pair.first)).c_str());
		//shm_unlink(("caching_info" + std::to_string(pair.first)).c_str()); 
		//shm_unlink(("caching_info_size" + std::to_string(pair.first)).c_str()); 
	}

	for (auto& p : data_buffers) {
		p.second.first->free_shm();
		p.second.second->free_shm();
	}

	shm_unlink("circular_buffer");
	shm_unlink("index_buf");
	buf_requests->free_shm();
	MPI_Finalize();
	exit(0);
}

void catch_sigterm() {
    static struct sigaction sigact;
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_sigaction = sig_term_handler;
	sigact.sa_flags = SA_SIGINFO;
	int res = sigaction(SIGTERM, &sigact, NULL);
	if (res == -1) {
		err_exit("sigaction for SIGTERM");
	}
}

int* create_shm_int(std::string shm_name) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
	const int size = sizeof(int);
	if (fd == -1)
		err_exit("shm_open");
	if (ftruncate(fd, size) == -1)
		err_exit("ftruncate");
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap create_shm_int");
//	if (close(fd) == -1);
//		err_exit("close");
	return (int*) p;
}

void write_file_location(const std::string& file_name, int rank, std::string path_to_write) {
        #ifdef CAPIOLOG
        logfile << "write file location before, file_name " << file_name << std::endl;
        #endif
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    int fd;
    if ((fd = open(file_name.c_str(), O_WRONLY|O_APPEND, 0664)) == -1) {
        logfile << "writer " << rank << " error opening file, errno = " << errno << " strerror(errno): " << strerror(errno) << std::endl;
        MPI_Finalize();
        exit(1);
    }
    // lock in exclusive mode
    lock.l_type = F_WRLCK;
    // lock entire file
    lock.l_whence = SEEK_SET; // offset base is start of the file
    lock.l_start = 0;         // starting offset is zero
    lock.l_len = 0;           // len is zero, which is a special value representing end
    // of file (no matter how large the file grows in future)
    lock.l_pid = getpid();
    if (fcntl(fd, F_SETLKW, &lock) == -1) { // F_SETLK doesn't block, F_SETLKW does
        logfile << "write " << rank << "failed to lock the file" << std::endl;
    }
    
	const char* path_to_write_cstr = path_to_write.c_str();
	const char* space_str = " ";
	const size_t len1 = strlen(path_to_write_cstr);
	const size_t len2 = strlen(space_str);
	const size_t len3 = strlen(node_name);
	char *file_location = (char*) malloc(len1 + len2 + len3 + 2); // +2 for  \n and for the null-terminator
	memcpy(file_location, path_to_write_cstr, len1);
	memcpy(file_location + len1, space_str, len2); 
	memcpy(file_location + len1 + len2, node_name, len3);
	file_location[len1 + len2 + len3] = '\n';
	file_location[len1 + len2 + len3 + 1] = '\0';
	write(fd, file_location, sizeof(char) * strlen(file_location));
    #ifdef CAPIOLOG
    	logfile << "writing " << file_location << std::endl;
    #endif
	files_location[path_to_write] = node_name;
	// Now release the lock explicitly.
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLKW, &lock) == - 1) {
        logfile << "write " << rank << "failed to unlock the file" << std::endl;
    }
	free(file_location);
    close(fd); // close the file: would unlock if needed
        #ifdef CAPIOLOG
        logfile << "write file location after" << std::endl;
        #endif
	return;
}

bool check_remote_file(const std::string& file_name, int rank, std::string path_to_check) {
    FILE * fp;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    bool res = true;
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;    /* shared lock for read*/
    lock.l_whence = SEEK_SET; /* base for seek offsets */
    lock.l_start = 0;         /* 1st byte in file */
    lock.l_len = 0;           /* 0 here means 'until EOF' */
    lock.l_pid = getpid();    /* process id */

    int fd; /* file descriptor to identify a file within a process */
    //fd = open(file_name.c_str(), O_RDONLY);  /* -1 signals an error */
    fp = fopen(file_name.c_str(), "r");
	if (fp == NULL) {
		logfile << "capio server " << rank << " failed to open the location file" << std::endl;
		return false;
	}
	fd = fileno(fp);
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        logfile << "capio server " << rank << " failed to lock the file" << std::endl;
        close(fd);
        return false;
    }
	const char* path_to_check_cstr = path_to_check.c_str();
	bool found = false;
    while ((read = getline(&line, &len, fp)) != -1 && !found) {
		char path[1024]; //TODO: heap memory
		int i = 0;
		while(line[i] != ' ') {
			path[i] = line[i];
			++i;
		}
		path[i] = '\0';
		char node_str[1024]; //TODO: heap memory 
		++i;
		int j = 0;
		while(line[i] != '\n') {
			node_str[j] = line[i];
			++i;
			++j;
		}
		node_str[j] = '\0';
		if (strcmp(path, path_to_check_cstr) == 0) {
			files_location[path_to_check] = (char*) malloc(sizeof(char) * (strlen(node_str) + 1));
			found = true;
			strcpy(files_location[path_to_check], node_str);
		}
		//check if the file is present
    }
	res = found;
    /* Release the lock explicitly. */
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        logfile << "reader " << rank << " failed to unlock the file" << std::endl;
        res = false;
    }
    fclose(fp);
    return res;
}

void flush_file_to_disk(int tid, int fd) {
	void* buf = std::get<0>(processes_files[tid][fd]);
	std::string path = processes_files_metadata[tid][fd];
	long int file_size = *std::get<1>(files_metadata[path]);	
	long int num_bytes_written, k = 0;
	while (k < file_size) {
    	num_bytes_written = write(fd, ((char*) buf) + k, (file_size - k));
    	k += num_bytes_written;
    }
}

void init_process(int tid) {
	
	#ifdef CAPIOLOG
	logfile << "init process tid " << std::to_string(tid) << std::endl;
	#endif	
	if (sems_write->find(tid) == sems_write->end()) {
	#ifdef CAPIOLOG
	logfile << "init process tid inside if " << std::to_string(tid) << std::endl;
	#endif	
		//sems_response[tid] = sem_open(("sem_response_read" + std::to_string(tid)).c_str(), O_RDWR);
		/*if (sems_response[tid] == SEM_FAILED) {
			err_exit("error creating sem_response_read" + std::to_string(tid));  	
		}
		*/
		(*sems_write)[tid] = sem_open(("sem_write" + std::to_string(tid)).c_str(), O_RDWR);
		if ((*sems_write)[tid] == SEM_FAILED) {
			err_exit("error creating sem_write" + std::to_string(tid));
		}
		Circular_buffer<off_t>* cb = new Circular_buffer<off_t>("buf_response" + std::to_string(tid), 8 * 1024 * 1024, sizeof(off_t));
		response_buffers.insert({tid, cb});
		std::string shm_name = "capio_write_data_buffer_tid_" + std::to_string(tid);
		auto* write_data_cb = new SPSC_queue<char>(shm_name, N_ELEMS_DATA_BUFS, WINDOW_DATA_BUFS);
		shm_name = "capio_read_data_buffer_tid_" + std::to_string(tid);
		auto* read_data_cb = new SPSC_queue<char>(shm_name, N_ELEMS_DATA_BUFS, WINDOW_DATA_BUFS);
		data_buffers.insert({tid, {write_data_cb, read_data_cb}});
		//caching_info[tid].first = (int*) get_shm("caching_info" + std::to_string(tid));
		//caching_info[tid].second = (int*) get_shm("caching_info_size" + std::to_string(tid));
	}
	#ifdef CAPIOLOG
	logfile << "end init process tid " << std::to_string(tid) << std::endl;
	#endif	

}
void create_file(std::string path, void* p_shm, bool is_dir) {
	std::string shm_name = path;
	std::replace(shm_name.begin(), shm_name.end(), '/', '_');
	shm_name = shm_name.substr(1);
	off64_t* p_shm_size = (off64_t*) create_shm(shm_name + "_size", sizeof(off64_t));	
	//logfile << "creating " << path << std::endl;
	//logfile << "pshm " << p_shm << "p_shm_size " << p_shm_size << std::endl;
	auto it = metadata_conf.find(path);
	if (it == metadata_conf.end()) {
        #ifdef CAPIOLOG
		logfile << "creating file withtout conf file " << path << std::endl;
		#endif
		files_metadata[path] = std::make_tuple(p_shm, p_shm_size, file_initial_size, true, Capio_file(is_dir), 0);
	}
	else {
		std::string committed = std::get<0>(it->second);
		std::string mode = std::get<1>(it->second);
        #ifdef CAPIOLOG
		logfile << "creating file " << path << std::endl;
		logfile << "committed " << committed << " mode " << mode << std::endl;
		#endif
		files_metadata[path] = std::make_tuple(p_shm, p_shm_size, file_initial_size, true, Capio_file(committed, mode, is_dir), 0);
	}

}

//TODO: function too long

void handle_open(char* str, char* p, int rank) {
	#ifdef CAPIOLOG
	logfile << "handle open" << std::endl;
	#endif
	int tid;
	tid = strtol(str + 5, &p, 10);
	init_process(tid);
	int fd = strtol(p + 1, &p, 10);
	std::string path(p + 1);
	void* p_shm;
	//int index = *caching_info[tid].second;
	//caching_info[tid].first[index] = fd;
	if (on_disk.find(path) == on_disk.end()) {
		std::string shm_name = path;
		std::replace(shm_name.begin(), shm_name.end(), '/', '_');
		shm_name = shm_name.substr(1);
		p_shm = new char[file_initial_size];
		//caching_info[tid].first[index + 1] = 0;
	}
	else {
		p_shm = nullptr;
		//caching_info[tid].first[index + 1] = 1;
	}
		//TODO: check the size that the user wrote in the configuration file
	off64_t* p_offset = (off64_t*) create_shm("offset_" + std::to_string(tid) + "_" + std::to_string(fd), sizeof(off64_t));
	processes_files[tid][fd] = std::make_tuple(p_shm, p_offset);//TODO: what happens if a process open the same file twice?
	//*caching_info[tid].second += 2;
	if (files_metadata.find(path) == files_metadata.end()) {
		create_file(path, p_shm, false);
	}
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	++c_file.n_opens;
    #ifdef CAPIOLOG
	logfile << "capio open n links " << c_file.n_links << " n opens " << c_file.n_opens << std::endl;;
	#endif
	processes_files_metadata[tid][fd] = path;
	int pid = pids[tid];
	auto it_files = writers.find(pid);
	if (it_files != writers.end()) {
		auto it_bools = it_files->second.find(path);
		if (it_bools == it_files->second.end()) {
			writers[pid][path] = false;
		}
	}
	else {
		writers[pid][path] = false;
	}
}

void send_data_to_client(int tid, char* buf, long int count) {
	auto* data_buf = data_buffers[tid].second;
	size_t n_writes = count / WINDOW_DATA_BUFS;
	size_t r = count % WINDOW_DATA_BUFS;
	size_t i = 0;
	while (i < n_writes) {
		data_buf->write(buf + i * WINDOW_DATA_BUFS);
		++i;
	}
	if (r)
		data_buf->write(buf + i * WINDOW_DATA_BUFS, r);
}

void handle_pending_read(int tid, int fd, long int process_offset, long int count) {
	
	std::string path = processes_files_metadata[tid][fd];
	Capio_file & c_file = std::get<4>(files_metadata[path]);
	off64_t end_of_sector = c_file.get_sector_end(process_offset);
	char* p = (char*) std::get<0>(files_metadata[path]);
	off64_t end_of_read = process_offset + count;
	size_t bytes_read;
	if (end_of_sector > end_of_read) {
		end_of_sector = end_of_read;
		bytes_read = count;
	}
	else
		bytes_read = end_of_sector - process_offset;
	response_buffers[tid]->write(&end_of_sector);
	send_data_to_client(tid, p + process_offset, bytes_read);
	//*processes_files[tid][fd].second += count;
	//TODO: check if the file was moved to the disk
}

struct handle_write_metadata{
	char str[64];
	long int rank;
};


std::string get_parent_dir_path(std::string file_path) {
	std::size_t i = file_path.rfind('/');
	if (i == std::string::npos) {
		logfile << "invalid file_path in get_parent_dir_path" << std::endl;
	}
	return file_path.substr(0, i);
}

/*
 * type == 0 -> regular entry
 * type == 1 -> "." entry
 * type == 2 -> ".." entry
 */
void write_entry_dir(int tid, std::string file_path, std::string dir, int type) {
	std::hash<std::string> hash;		
	struct linux_dirent ld;
	ld.d_ino = hash(file_path);
	std::string file_name;
	if (type == 0) {
		std::size_t i = file_path.rfind('/');
		if (i == std::string::npos) {
			logfile << "invalid file_path in get_parent_dir_path" << std::endl;
		}
		file_name = file_path.substr(i + 1);
	}
	else if (type == 1) {
		file_name = ".";
	}
	else {
		file_name = "..";
	}

	strcpy(ld.d_name, file_name.c_str());
	long int ld_size = theoretical_size;
	ld.d_reclen =  ld_size;
	auto it_tuple = files_metadata.find(dir);

	if (it_tuple == files_metadata.end()) {
		logfile << "dir " << dir << " is not present in CAPIO" << std::endl;
		exit(1);
	}
	void* file_shm = std::get<0>(it_tuple->second);
	off64_t file_size = *std::get<1>(it_tuple->second);
	off64_t file_shm_size = std::get<2>(it_tuple->second);
	off64_t data_size = file_size + ld_size;
	ld.d_off =  data_size;
		if (data_size > file_shm_size) {

        #ifdef CAPIOLOG
        logfile << "handle write data_size > file_shm_size" << std::endl;
        #endif
			//remap
			size_t new_size;
			if (data_size > file_shm_size * 2)
				new_size = data_size;
			else
				new_size = file_shm_size * 2;


			char* old_p = (char*)std::get<0>(files_metadata[dir]);
			char* new_p = new char[new_size];
			memcpy(new_p, old_p, data_size); //TODO: slow
			delete [] old_p;
			std::get<0>(files_metadata[dir]) = new_p;
			std::get<2>(files_metadata[dir]) = new_size;
		}
		if (std::get<4>(files_metadata[file_path]).is_dir()) {
			ld.d_name[DNAME_LENGTH + 1] = DT_DIR; 
		}
		else {
			ld.d_name[DNAME_LENGTH + 1] = DT_REG; 
		}
			ld.d_name[DNAME_LENGTH] = '\0'; 
		memcpy((char*) file_shm + file_size, &ld, sizeof(ld));
		*std::get<1>(it_tuple->second) = data_size;
		Capio_file& c_file = std::get<4>(files_metadata[dir]);
	off64_t base_offset = file_size;
		#ifdef CAPIOLOG
		logfile << "insert sector for dir" << base_offset << ", " << data_size << std::endl;
		#endif
		c_file.insert_sector(base_offset, data_size);
		++c_file.n_files;
		std::string committed = c_file.get_committed();
		int pid = pids[tid];
		writers[pid][dir] = true;
		int n_committed;
		try {
			n_committed = std::stoi(committed);
			#ifdef CAPIOLOG
			logfile << "nfiles in dir " << dir << " " << c_file.n_files << std::endl;
			#endif
			if (c_file.n_files == n_committed) {
				#ifdef CAPIOLOG
				logfile << "dir completed " << std::endl;
				#endif
				c_file.complete = true;
			}
		}
		catch (const std::exception& e) {
			#ifdef CAPIOLOG
			logfile << "exception write entry: " << e.what() << std::endl;
			logfile << "committed " << committed << std::endl;
			#endif
		}

			#ifdef CAPIOLOG
		c_file.print(logfile);

			#endif
}

void update_dir(int tid, std::string file_path, int rank) {
	std::string dir = get_parent_dir_path(file_path);
        #ifdef CAPIOLOG
        logfile << "update dir " << dir << std::endl;
        #endif
    if (std::get<3>(files_metadata[dir])) {
		std::get<3>(files_metadata[dir]) = false;
        write_file_location("files_location.txt", rank, dir);
	}
        #ifdef CAPIOLOG
        logfile << "before write entry dir" << std::endl;
        #endif
	write_entry_dir(tid, file_path, dir, 0);
        #ifdef CAPIOLOG
        logfile << "update dir end" << std::endl;
        #endif
	return;
}



void handle_pending_remote_reads(std::string path, off64_t data_size) {
        auto it_client = clients_remote_pending_reads.find(path);
        if (it_client !=  clients_remote_pending_reads.end()) {
	std::list<std::tuple<size_t, size_t, sem_t*>>::iterator it_list, prev_it_list;
	it_list = it_client->second.begin();
                while (it_list != it_client->second.end()) {
                        off64_t offset = std::get<0>(*it_list);
                        off64_t nbytes = std::get<1>(*it_list);
                        sem_t* sem = std::get<2>(*it_list);
        #ifdef CAPIOLOG
        logfile << "handle serving remote pending reads inside the loop" << std::endl;
        #endif
                        if (offset + nbytes < data_size) {
                                sem_post(sem);
                                if (it_list == it_client->second.begin()) {
                                        it_client->second.erase(it_list);
                                        it_list = it_client->second.begin();
                                }
                                else {
                                        it_client->second.erase(it_list);
                                        it_list = std::next(prev_it_list);
                                }
                        }
                        else {
                                prev_it_list = it_list;
                                ++it_list;
                        }
                }
	}
		return;
}

void handle_write(const char* str, int rank) {
        //check if another process is waiting for this data
        std::string request;
        int tid, fd;
		off_t base_offset;
        off64_t count;
        off64_t data_size;
        std::istringstream stream(str);
        stream >> request >> tid >> fd >> base_offset >> count;
		data_size = base_offset + count;
        std::string path = processes_files_metadata[tid][fd];
		char* p = (char *) std::get<0>(files_metadata[path]);
		auto* data_buf = data_buffers[tid].first;
		size_t n_reads = count / WINDOW_DATA_BUFS;  
		size_t r = count % WINDOW_DATA_BUFS;
		size_t i = 0;
		p = p + base_offset;
        #ifdef CAPIOLOG
		logfile << "debug handle_write 0 " << std::endl;
        #endif
		while (i < n_reads) {
        #ifdef CAPIOLOG
		logfile << "debug handle_write 2 " << std::endl;
        #endif
			data_buf->read(p + i * WINDOW_DATA_BUFS);
			++i;
		}
        #ifdef CAPIOLOG
		logfile << "debug handle_write 3 " << std::endl;
        #endif
		if (r)
			data_buf->read(p + i * WINDOW_DATA_BUFS, r);
        #ifdef CAPIOLOG
		logfile << "debug handle_write 4 " << std::endl;
        #endif
		int pid = pids[tid];
		writers[pid][path] = true;
		Capio_file& c_file = std::get<4>(files_metadata[path]);
        #ifdef CAPIOLOG
		logfile << "insert sector " << base_offset << ", " << data_size << std::endl;
        #endif
		c_file.insert_sector(base_offset, data_size);
        #ifdef CAPIOLOG
		c_file.print(logfile);
        logfile << "handle write tid fd " << tid << " " << fd << std::endl;
		logfile << "path " << path << std::endl;
        #endif
        if (std::get<3>(files_metadata[path])) {
			std::get<3>(files_metadata[path]) = false;
            write_file_location("files_location.txt", rank, path);
			//TODO: it works only if there is one prod per file
			update_dir(tid, path, rank);
        }
        *std::get<1>(files_metadata[path]) = data_size; 
		off64_t file_shm_size = std::get<2>(files_metadata[path]);
		if (data_size > file_shm_size) {

        #ifdef CAPIOLOG
        logfile << "handle write data_size > file_shm_size" << std::endl;
        #endif
			//remap
			size_t new_size;
			if (data_size > file_shm_size * 2)
				new_size = data_size;
			else
				new_size = file_shm_size * 2;


			char* old_p = (char*)std::get<0>(files_metadata[path]);
			char* new_p = new char[new_size];
			memcpy(new_p, old_p, data_size); //TODO: slow
			delete [] old_p;
			std::get<0>(files_metadata[path]) = old_p;
			std::get<2>(files_metadata[path]) = new_size;
		}
        /*total_bytes_shm += data_size;
        if (total_bytes_shm > max_shm_size && on_disk.find(path) == on_disk.end()) {
                shm_full = true;
                flush_file_to_disk(tid, fd);
                int i = 0;
                bool found = false;
                while (!found && i < *caching_info[tid].second) {
                        if (caching_info[tid].first[i] == fd) {
                                found = true;
                        }
                        else {
                                i += 2;
								                       }
                }
                if (i >= *caching_info[tid].second) {
                        MPI_Finalize();
                        exit(1);
                }
                if (found) {
                        caching_info[tid].first[i + 1] = 1;
                }
                on_disk.insert(path);
        }*/
        //sem_post(sems_response[tid]);
        auto it = pending_reads.find(path);
	std::string mode = std::get<4>(files_metadata[path]).get_mode();
        #ifdef CAPIOLOG
        logfile << "mode is " << mode << std::endl;
        #endif
        //sem_wait(sems_write[tid]);
        if (it != pending_reads.end() && mode == "append") {
        #ifdef CAPIOLOG
        logfile << "There were pending reads for" << path << std::endl;
        #endif
                auto& pending_reads_this_file = it->second;
				auto it_vec = pending_reads_this_file.begin();
                while (it_vec != pending_reads_this_file.end()) {
                        auto tuple = *it_vec;
                        int pending_tid = std::get<0>(tuple);
                        int fd = std::get<1>(tuple);
                        size_t process_offset = *std::get<1>(processes_files[pending_tid][fd]);
                        size_t count = std::get<2>(tuple);
                        size_t file_size = *std::get<1>(files_metadata[path]);
        #ifdef CAPIOLOG
        logfile << "pending read offset " << process_offset << " count " << count << " file_size " << file_size << std::endl;
        #endif
                        if (process_offset + count <= file_size) {
        #ifdef CAPIOLOG
        logfile << "handling this pending read"<< std::endl;
        #endif
                                handle_pending_read(pending_tid, fd, process_offset, count);
                        it_vec = pending_reads_this_file.erase(it_vec);
                        }
						else
							++it_vec;
                }
        }
        //sem_post(sems_write[tid]);

	if (mode == "append") {
        #ifdef CAPIOLOG
        logfile << "handle write serving remote pending reads" << std::endl;
        #endif
		handle_pending_remote_reads(path, data_size);
	}
	

}

/*
 * Multithread function
 */

void handle_local_read(int tid, int fd, off64_t count, bool dir) {
		#ifdef CAPIOLOG
		logfile << "handle local read" << std::endl;
		#endif
		sem_wait(&handle_local_read_sem);
		std::string path = processes_files_metadata[tid][fd];
		Capio_file & c_file = std::get<4>(files_metadata[path]);
		off64_t process_offset = *std::get<1>(processes_files[tid][fd]);
		int pid = pids[tid];
		bool writer = writers[pid][path];
		off64_t end_of_sector = c_file.get_sector_end(process_offset);
		#ifdef CAPIOLOG
		logfile << "process offset " << process_offset << std::endl;
		logfile << "count " << count << std::endl;
		logfile << "end of sector" << end_of_sector << std::endl;
		c_file.print(logfile);
        #endif
		off64_t end_of_read = process_offset + count;
		off64_t nreads;
		std::string committed = c_file.get_committed();
		std::string mode = c_file.get_mode();
		if (mode != "append" && !c_file.complete) {
			#ifdef CAPIOLOG
			logfile << "add pending reads 1" << std::endl;
			logfile << "mode " << mode << std::endl;
			logfile << "file complete " << c_file.complete << std::endl;
			#endif
			pending_reads[path].push_back(std::make_tuple(tid, fd, count));
		}
		else if (end_of_read > end_of_sector) {
		#ifdef CAPIOLOG
		logfile << "Am I a writer? " << writer << std::endl;
		logfile << "Is the file completed? " << c_file.complete << std::endl;
		#endif
			if (!writer && !c_file.complete && !dir) {
				#ifdef CAPIOLOG
				logfile << "add pending reads 2" << std::endl;
				#endif
				pending_reads[path].push_back(std::make_tuple(tid, fd, count));
			}
			else {
				//off64_t file_size = c_file.get_file_size(); //TODO: why file size and not end of sector?
					nreads = end_of_sector;

				char* p = (char*) std::get<0>(files_metadata[path]);
		#ifdef CAPIOLOG
		logfile << "debug bbb end_of_sector " << end_of_sector << " nreads " << nreads << " count " << count << " process_offset " << process_offset << std::endl;
		#endif
				response_buffers[tid]->write(&end_of_sector);
				send_data_to_client(tid, p + process_offset, end_of_sector - process_offset);
			}
				

		}
		else {
			char* p = (char*) std::get<0>(files_metadata[path]);
			size_t bytes_read;
			bytes_read = count;
		#ifdef CAPIOLOG
		logfile << "debug aaa end_of_sector " << end_of_sector << " bytes_read " << bytes_read << " count " << count << " process_offset " << process_offset << std::endl;
		#endif
			response_buffers[tid]->write(&end_of_read);
			send_data_to_client(tid, p + process_offset, bytes_read);
		}
		#ifdef CAPIOLOG
		logfile << "process offset " << process_offset << std::endl;
		#endif
		sem_post(&handle_local_read_sem);
		//*processes_files[tid][fd].second += count;
		//TODO: check if the file was moved to the disk
}

bool read_from_local_mem(int tid, off64_t process_offset, off64_t end_of_read,
		off64_t end_of_sector, off64_t count, std::string path) {
	#ifdef CAPIOLOG
	logfile << "reading from local memory" << std::endl;
	#endif
	bool res = false;
	if (end_of_read <= end_of_sector) {
		char* p = (char*) std::get<0>(files_metadata[path]);
		response_buffers[tid]->write(&end_of_read);
		send_data_to_client(tid, p + process_offset, count);
		res = true;
	}
	return res;
}

/*
 * Multithread function
 *
 */

void handle_remote_read(int tid, int fd, off64_t count, int rank, bool dir) {
		#ifdef CAPIOLOG
		logfile << "handle remote read before sem_wait" << std::endl;
		#endif
		//before sending the request to the remote node, it checks
		//in the local cache

		sem_wait(&handle_remote_read_sem);

		std::string path = processes_files_metadata[tid][fd];
		Capio_file & c_file = std::get<4>(files_metadata[path]);
		size_t real_file_size = std::get<5>(files_metadata[path]);
		off64_t process_offset = *std::get<1>(processes_files[tid][fd]);
		off64_t end_of_read = process_offset + count;
		off64_t end_of_sector = c_file.get_sector_end(process_offset);

		if (c_file.complete && (end_of_read <= end_of_sector || end_of_sector == real_file_size)) {
			handle_local_read(tid, fd, count, dir);
			sem_post(&handle_remote_read_sem);
			return;
		}
		bool res = read_from_local_mem(tid, process_offset, end_of_read, end_of_sector, count, path); //when is not complete but mode = append
		if (res) { // it means end_of_read < end_of_sector
			sem_post(&handle_remote_read_sem);
			return;
		}

		// If it is not in cache then send the request to the remote node
		const char* msg;
		std::string str_msg;
		int dest = nodes_helper_rank[files_location[processes_files_metadata[tid][fd]]];
		size_t offset = *std::get<1>(processes_files[tid][fd]);
		str_msg = "read " + processes_files_metadata[tid][fd] + " " + std::to_string(rank) + " " + std::to_string(offset) + " " + std::to_string(count); 
		msg = str_msg.c_str();
		#ifdef CAPIOLOG
		logfile << "handle remote read" << std::endl;
		logfile << "msg sent " << msg << std::endl;
		logfile << processes_files_metadata[tid][fd] << std::endl;
		logfile << "dest " << dest << std::endl;
		logfile << "rank" << rank << std::endl;
		#endif
		MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
		my_remote_pending_reads[processes_files_metadata[tid][fd]].push_back(std::make_tuple(tid, fd, count));
		sem_post(&handle_remote_read_sem);
}

struct wait_for_file_metadata{
	int tid;
	int fd;
	bool dir;
	off64_t count;
};

void loop_check_files_location(std::string path_to_check, int rank) {
	//check if the data is created
	FILE* fp;
	bool found = false;
	char * line = NULL;
    size_t len = 0;
    ssize_t read;
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;    /* shared lock for read*/
    lock.l_whence = SEEK_SET; /* base for seek offsets */
    lock.l_start = 0;         /* 1st byte in file */
    lock.l_len = 0;           /* 0 here means 'until EOF' */
    lock.l_pid = getpid();    /* process id */
    int fd_locations; /* file descriptor to identify a file within a process */

	
	#ifdef CAPIOLOG
	logfile << "wait for file before" << std::endl;
	#endif
	
	const char* path_to_check_cstr = path_to_check.c_str();

	struct timespec sleepTime;
    struct timespec returnTime;
    sleepTime.tv_sec = 0;
    sleepTime.tv_nsec = 200000;
	while (!found) {


		nanosleep(&sleepTime, &returnTime);
		int tot_chars = 0;
    		fp = fopen("files_location.txt", "r");
		if (fp == NULL) {
			logfile << "error fopen files_location.txt" << std::endl;
			exit(1);
		}
		fd_locations = fileno(fp);
		if (fcntl(fd_locations, F_SETLKW, &lock) < 0) {
			err_exit("error fcntl files_location.txt");
			exit(1);
    	}
		fseek(fp, tot_chars, SEEK_SET);

        while ((read = getline(&line, &len, fp)) != -1 && !found) {
			if (read != -1) {
				tot_chars += read;
				char path[1024]; //TODO: heap memory
				int i = 0;
				while(line[i] != ' ') {
					path[i] = line[i];
					++i;
				}
				path[i] = '\0';
				char node_str[1024]; //TODO: heap memory 
				++i;
				int j = 0;
				while(line[i] != '\n') {
					node_str[j] = line[i];
					++i;
					++j;
				}
				node_str[j] = '\0';
				files_location[path_to_check] = (char*) malloc(sizeof(node_str) + 1); //TODO:free the memory
				if (strcmp(path, path_to_check_cstr) == 0) {
					found = true;
					strcpy(files_location[path_to_check], node_str);
				}
			}
		}
		/* Release the lock explicitly. */
		lock.l_type = F_UNLCK;
		if (fcntl(fd_locations, F_SETLK, &lock) < 0) {
			logfile << "reader " << rank << " failed to unlock the file" << std::endl;
		}
		fclose(fp);
	}
}

void* wait_for_file(void* pthread_arg) {
	struct wait_for_file_metadata* metadata = (struct wait_for_file_metadata*) pthread_arg;
	int tid = metadata->tid;
	int fd = metadata-> fd;
	off64_t count = metadata->count;
	bool dir = metadata->dir;

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	std::string path_to_check(processes_files_metadata[tid][fd]);
	loop_check_files_location(path_to_check, rank);

	//check if the file is local or remote
	if (strcmp(files_location[path_to_check], node_name) == 0) {
			handle_local_read(tid, fd, count, dir);
	}
	else {
			#ifdef CAPIOLOG
			logfile << "handle remote read in wait for file" << std::endl;	
			logfile << "path to check " << path_to_check << std::endl;
			#endif
			handle_remote_read(tid, fd, count, rank, dir);
	}

	free(metadata);
	return nullptr;
}


void handle_read(char* str, int rank, bool dir) {
	#ifdef CAPIOLOG
	logfile << "handle read str" << str << std::endl;
	#endif
	std::string request;
	int tid, fd;
	off64_t count;
	std::istringstream stream(str);
	stream >> request >> tid >> fd >> count;
	if (files_location.find(processes_files_metadata[tid][fd]) == files_location.end()) {
		check_remote_file("files_location.txt", rank, processes_files_metadata[tid][fd]);
		if (files_location.find(processes_files_metadata[tid][fd]) == files_location.end()) {
			std::string path = processes_files_metadata[tid][fd];
			//launch a thread that checks when the file is created
			pthread_t t;
			struct wait_for_file_metadata* metadata = (struct wait_for_file_metadata*)  malloc(sizeof(wait_for_file_metadata));
			metadata->tid = tid;
			metadata->fd = fd;
			metadata->count = count;
			metadata->dir = dir;
			int res = pthread_create(&t, NULL, wait_for_file, (void*) metadata);
			if (res != 0) {
				logfile << "error creation of capio server thread" << std::endl;
				MPI_Finalize();
				exit(1);
			}

			return;
		}
	}
	if (strcmp(files_location[processes_files_metadata[tid][fd]], node_name) == 0) {
		handle_local_read(tid, fd, count, dir);
	}
	else {
	#ifdef CAPIOLOG
	logfile << "before handle remote read handle read" << std::endl;
	#endif	
		handle_remote_read(tid, fd, count, rank, dir);
	}
	
}


void delete_file(std::string path) {
	#ifdef CAPIOLOG
	logfile << "deleting file " << path << std::endl;
	#endif	
	shm_unlink(path.c_str());
	shm_unlink((path + "_size").c_str());
	files_metadata.erase(path);

}

void handle_close(int tid, int fd) {
	std::string path = processes_files_metadata[tid][fd];
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	if (c_file.get_committed() == "on_close") {
		#ifdef CAPIOLOG
		logfile <<  "handle close, committed = on_close" << std::endl;
		#endif
		c_file.complete = true;
		auto it = pending_reads.find(path);
        if (it != pending_reads.end()) {
	#ifdef CAPIOLOG
	logfile << "handle pending read file on_close " << path << std::endl;
	#endif	
            auto& pending_reads_this_file = it->second;
			auto it_vec = pending_reads_this_file.begin();
            while (it_vec != pending_reads_this_file.end()) {
                auto tuple = *it_vec;
                int pending_tid = std::get<0>(tuple);
                int fd = std::get<1>(tuple);
                size_t process_offset = *std::get<1>(processes_files[pending_tid][fd]);
                size_t count = std::get<2>(tuple);
				#ifdef CAPIOLOG
				logfile << "pending read tid fd offset count " << tid << " " << fd << " " << process_offset <<" "<< count << std::endl;
				#endif	
                handle_pending_read(pending_tid, fd, process_offset, count);
                it_vec = pending_reads_this_file.erase(it_vec);
            }
			pending_reads.erase(it);
        }
		auto it_client = clients_remote_pending_stat.find(path);
        if (it_client !=  clients_remote_pending_stat.end()) {
			for (sem_t* sem : it_client->second)
				sem_post(sem);
			clients_remote_pending_stat.erase(path);
		}
		
	}
	handle_pending_remote_reads(path, c_file.get_sector_end(0));
	//TODO: error if seek are done

	--c_file.n_opens;
	#ifdef CAPIOLOG
	logfile << "capio close n links " << c_file.n_links << " n opens " << c_file.n_opens << std::endl;;
	#endif
	if (c_file.n_opens == 0 && c_file.n_links <= 0)
		delete_file(path);
	shm_unlink(("offset_" + std::to_string(tid) +  "_" + std::to_string(fd)).c_str());
	processes_files[tid].erase(fd);
	processes_files_metadata[tid].erase(fd);
}

void handle_close(char* str, char* p) {
	int tid, fd;
	sscanf(str, "clos %d %d", &tid, &fd);
	#ifdef CAPIOLOG
	logfile << "handle close " << tid << " " << fd << std::endl;
	#endif
	handle_close(tid, fd);
}

void handle_remote_read(char* str, char* p, int rank) {
	size_t bytes_received, offset, file_size;
	char path_c[PATH_MAX];
	int complete_tmp;
	sscanf(str, "ream %s %zu %zu %d %zu", path_c, &bytes_received, &offset, &complete_tmp, &file_size);
	#ifdef CAPIOLOG
		logfile << "serving the remote read: " << str << std::endl;
	#endif
	bool complete = complete_tmp;
	*std::get<1>(files_metadata[path_c]) = offset + bytes_received;
	std::get<5>(files_metadata[path_c]) = file_size;
	Capio_file& c_file = std::get<4>(files_metadata[path_c]);
    #ifdef CAPIOLOG
	logfile << "insert offset " << offset << " bytes_received " << bytes_received << std::endl;
#endif
	c_file.insert_sector(offset, offset + bytes_received);
	c_file.complete = complete;
	std::string path(path_c);
	int tid, fd;
	long int count; //TODO: diff between count and bytes_received
	std::list<std::tuple<int, int, long int>>& list_remote_reads = my_remote_pending_reads[path];
	auto it = list_remote_reads.begin();
	std::list<std::tuple<int, int, long int>>::iterator prev_it;
	off64_t end_of_sector;
	while (it != list_remote_reads.end()) {
		tid = std::get<0>(*it);
		fd = std::get<1>(*it);
		count = std::get<2>(*it);
		size_t fd_offset = *std::get<1>(processes_files[tid][fd]);
		if (complete || fd_offset + count <= offset + bytes_received) {
		#ifdef CAPIOLOG
			logfile << "handling others remote reads fd_offset " << fd_offset << " count " << count << " offset " << offset << " bytes received " << bytes_received << std::endl;
		#endif
			//this part is equals to the local read (TODO: function)
		 	end_of_sector = c_file.get_sector_end(fd_offset);
    #ifdef CAPIOLOG
			logfile << "end of sector " << end_of_sector << std::endl;
#endif
			char* p = (char*) std::get<0>(files_metadata[path]);

			size_t bytes_read;
			off64_t end_of_read = fd_offset + count;
			if (end_of_sector > end_of_read) {
				end_of_sector = end_of_read;
				bytes_read = count;
			}
			else
				bytes_read = end_of_sector - fd_offset;

			response_buffers[tid]->write(&end_of_sector);
			send_data_to_client(tid, p + fd_offset, bytes_read);

			if (it == list_remote_reads.begin()) {
				list_remote_reads.erase(it);
				it = list_remote_reads.begin();
			}
			else {
				list_remote_reads.erase(it);
				it = std::next(prev_it);
			}
		}
		else {
			prev_it = it;
			++it;
		}
	}
}

void handle_lseek(char* str) {
	int tid, fd;	
	size_t offset;
	sscanf(str, "seek %d %d %zu", &tid, &fd, &offset);
	std::string path = processes_files_metadata[tid][fd];
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	off64_t sector_end = c_file.get_sector_end(offset);
	response_buffers[tid]->write(&sector_end);
}

void handle_sdat(char* str) {
	int tid, fd;	
	size_t offset;
	sscanf(str, "sdat %d %d %zu", &tid, &fd, &offset);
	std::string path = processes_files_metadata[tid][fd];
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	off64_t res = c_file.seek_data(offset);
	response_buffers[tid]->write(&res);
}

void handle_shol(char* str) {
	int tid, fd;	
	size_t offset;
	sscanf(str, "shol %d %d %zu", &tid, &fd, &offset);
	std::string path = processes_files_metadata[tid][fd];
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	off64_t res = c_file.seek_hole(offset);
	response_buffers[tid]->write(&res);
}

void handle_seek_end(char* str) {
	int tid, fd;	
	sscanf(str, "send %d %d", &tid, &fd);
	std::string path = processes_files_metadata[tid][fd];
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	off64_t res = c_file.get_file_size();
	response_buffers[tid]->write(&res);
}

void close_all_files(int tid) {
	auto it_process_files = processes_files.find(tid);
	if (it_process_files != processes_files.end()) {
		auto process_files = it_process_files->second;
		for (auto it : process_files) {
			handle_close(tid, it.first);
		}
	}
}

void handle_exig(char* str) {
	int tid;
	sscanf(str, "exig %d", &tid);
	#ifdef CAPIOLOG
	logfile << "handle exit group " << std::endl;
	#endif	
   // std::unordered_map<int, std::unordered_map<std::string, bool>> writers;
   int pid = pids[tid];
   auto files = writers[pid];
   for (auto& pair : files) {
		std::string path = pair.first;	
   	if (pair.second) {
		auto it_conf = metadata_conf.find(path);
		#ifdef CAPIOLOG
		logfile << "path: " << path << std::endl;
		#endif
		if (it_conf == metadata_conf.end() || std::get<0>(it_conf->second) == "on_termination" || std::get<0>(it_conf->second).length() == 0) { 
			Capio_file& c_file = std::get<4>(files_metadata[path]);
			#ifdef CAPIOLOG
			logfile << "committed " << c_file.get_committed() << std::endl;
			#endif
			if (c_file.is_dir()) {
				std::string committed = c_file.get_committed();
				int n_committed;
				try {
					n_committed = std::stoi(committed);
					#ifdef CAPIOLOG
					logfile << "nfiles in dir " << path << " " << c_file.n_files << std::endl;
					#endif
					if (n_committed > c_file.n_files) {
						#ifdef CAPIOLOG
						logfile << "dir " << path << " completed " << std::endl;
						#endif
						c_file.complete = true;
					}
				}
				catch (const std::exception& e) {
					#ifdef CAPIOLOG
					logfile << "exception write entry: " << e.what() << std::endl;
					logfile << "committed " << committed << std::endl;
					logfile << "dir " << path << " completed " << std::endl;
					#endif
					c_file.complete = true;
				}
			}
			else {
				#ifdef CAPIOLOG
				logfile << "file " << path << " completed" << std::endl;
				#endif
				c_file.complete = true;
			}
		}
		else {
			#ifdef CAPIOLOG
			logfile << "committed " << std::get<0>(it_conf->second) << " mode " << std::get<1>(it_conf->second) << std::endl;
			#endif
		}
	//#ifdef CAPIOLOG
	//logfile << "handle exit group wait before" << std::endl;
	//#endif	
        //sem_wait(sems_write[tid]);
	//#ifdef CAPIOLOG
	//logfile << "handle exit group wait after" << std::endl;
	//#endif	
		auto it = pending_reads.find(path);
        if (it != pending_reads.end()) {
	#ifdef CAPIOLOG
	logfile << "handle pending read file on_termination " << path << std::endl;
	#endif	
                auto& pending_reads_this_file = it->second;
				auto it_vec = pending_reads_this_file.begin();
                while (it_vec != pending_reads_this_file.end()) {
                        auto tuple = *it_vec;
                        int pending_tid = std::get<0>(tuple);
                        int fd = std::get<1>(tuple);
                        size_t process_offset = *std::get<1>(processes_files[pending_tid][fd]);
                        size_t count = std::get<2>(tuple);
						#ifdef CAPIOLOG
						logfile << "pending read tid fd offset count " << tid << " " << fd << " " << process_offset <<" "<< count << std::endl;
						#endif	
                        handle_pending_read(pending_tid, fd, process_offset, count);
                        it_vec = pending_reads_this_file.erase(it_vec);
                }
			pending_reads.erase(it);
        }
        //sem_post(sems_write[tid]);
	//#ifdef CAPIOLOG
	//logfile << "handle exit group 2" << std::endl;
	//#endif	
	}
   }
	#ifdef CAPIOLOG
	logfile << "handle exit group 3" << std::endl;
	#endif	
   close_all_files(tid);
}


void handle_local_stat(int tid, const char* path) {
	off64_t file_size;
	auto it = files_metadata.find(path);
	Capio_file& c_file = std::get<4>(it->second);
	file_size = c_file.get_file_size();
	off64_t is_dir;
	if (c_file.is_dir())
		is_dir = 0;
	else
		is_dir = 1;
	response_buffers[tid]->write(&file_size);
	response_buffers[tid]->write(&is_dir);
	#ifdef CAPIOLOG
		logfile << "file size stat : " << file_size << std::endl;
	#endif
}

void handle_remote_stat(int tid, const std::string path, int rank) {
	#ifdef CAPIOLOG
	logfile << "handle remote read before sem_wait" << std::endl;
	#endif
	sem_wait(&handle_remote_stat_sem);
	std::string str_msg;
	int dest = nodes_helper_rank[files_location[path]];
	str_msg = "stat " + std::to_string(rank) + " " + path; 
	const char* msg = str_msg.c_str();
	#ifdef CAPIOLOG
	logfile << "handle remote stat" << std::endl;
	logfile << "msg sent " << msg << std::endl;
	logfile << "dest " << dest << std::endl;
	logfile << "rank" << rank << std::endl;
	#endif
	MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
	my_remote_pending_stats[path].push_back(tid);
	sem_post(&handle_remote_stat_sem);

}

struct wait_for_stat_metadata{
	int tid;
	char path[PATH_MAX];
};

void* wait_for_stat(void* pthread_arg) {
	struct wait_for_stat_metadata* metadata = (struct wait_for_stat_metadata*) pthread_arg;
	int tid = metadata->tid;
	const char* path = metadata->path;
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	std::string path_to_check(path);
	loop_check_files_location(path_to_check, rank);

	//check if the file is local or remote

	Capio_file& c_file = std::get<4>(files_metadata[path]);
	if (strcmp(files_location[path_to_check], node_name) == 0 || c_file.get_mode() == "append") {
		handle_local_stat(tid, path);
	}
	else {
		handle_remote_stat(tid, path, rank);
	}
	


	free(metadata);
	return nullptr;
}


void handle_stat(const char* str, int rank) {
	char path[2048];
	int tid;
	sscanf(str, "stat %d %s", &tid, path);

	init_process(tid);

	char* p_shm;
	if (files_location.find(path) == files_location.end()) {
		check_remote_file("files_location.txt", rank, path);
		if (files_location.find(path) == files_location.end()) {
			//if it is in configuration file then wait otherwise fails
			
//std::unordered_map<std::string, std::tuple<std::string, std::string>> metadata_conf;
			if (metadata_conf.find(path) != metadata_conf.end()) {
				if (files_metadata.find(path) == files_metadata.end()) {
					p_shm = new char[file_initial_size];
					create_file(path, p_shm, false);
				}

				pthread_t t;
				struct wait_for_stat_metadata* metadata = (struct wait_for_stat_metadata*)  malloc(sizeof(wait_for_stat_metadata));
				metadata->tid = tid;
				strcpy(metadata->path, path);
				int res = pthread_create(&t, NULL, wait_for_stat, (void*) metadata);
				if (res != 0) {
					logfile << "error creation of capio server thread wiat for stat" << std::endl;
					MPI_Finalize();
					exit(1);
				}
			} 
			else {
				off64_t file_size;
				file_size = -1;	
				response_buffers[tid]->write(&file_size);
	#ifdef CAPIOLOG
		logfile << "file size stat : " << file_size << std::endl;
	#endif
			}

			return;
		}
	}
	if (files_metadata.find(path) == files_metadata.end()) {
		p_shm = new char[file_initial_size];
		create_file(path, p_shm, false);
	}

	Capio_file& c_file = std::get<4>(files_metadata[path]);

	if (strcmp(files_location[path], node_name) == 0 || c_file.get_mode() == "append") {
		handle_local_stat(tid, path);
	}
	else {
		handle_remote_stat(tid, path, rank);
	}
	

}

void handle_fstat(const char* str, int rank) {
	int tid, fd;
	sscanf(str, "fsta %d %d", &tid, &fd);
	std::string path = processes_files_metadata[tid][fd];
	#ifdef CAPIOLOG
	logfile << "path " << path << std::endl;
	#endif

	if (files_location.find(path) == files_location.end()) {
		check_remote_file("files_location.txt", rank, path);
		if (files_location.find(path) == files_location.end()) {
			//TODO: fix this
			handle_local_stat(tid, path.c_str());
			return;
		}

	}

	Capio_file& c_file = std::get<4>(files_metadata[path]);

	if (strcmp(files_location[path], node_name) == 0 || c_file.get_mode() == "append") {
		handle_local_stat(tid, path.c_str());
	}
	else {
		handle_remote_stat(tid, path, rank);
	}

}

void handle_access(const char* str) {
	int tid;
	char path[PATH_MAX];
	#ifdef CAPIOLOG
		logfile << "handle access: " << str << std::endl;
	#endif
	sscanf(str, "accs %d %s", &tid, path);
	off64_t res;
	auto it = files_location.find(path);
	if (it == files_location.end())
		res = -1;
	else 
		res = 0;
	response_buffers[tid]->write(&res);
}


void handle_unlink(const char* str) {
	char path[PATH_MAX];
	off64_t res;
	int tid;
	#ifdef CAPIOLOG
		logfile << "handle unlink: " << str << std::endl;
	#endif
	sscanf(str, "unlk %d %s", &tid, path);
	auto it = files_metadata.find(path);
	if (it != files_metadata.end()) { //TODO: it works only in the local case
		Capio_file& c_file = std::get<4>(it->second);
		--c_file.n_links;
		#ifdef CAPIOLOG
		logfile << "capio unlink n links " << c_file.n_links << " n opens " << c_file.n_opens;
		#endif
		if (c_file.n_opens == 0 && c_file.n_links <= 0) {
			delete_file(path);
		}
		res = 0;
	}
	else
		res = -1;
	response_buffers[tid]->write(&res);
}

std::unordered_set<std::string> get_paths_opened_files(pid_t tid) {
	std::unordered_set<std::string> set;
	for (auto& it : processes_files_metadata[tid])
		set.insert(it.second);
	return set;
}

//TODO: caching info
void handle_clone(const char* str) {
	pid_t ptid, new_tid;
	sscanf(str, "clon %d %d", &ptid, &new_tid);
	init_process(new_tid);
	processes_files[new_tid] = processes_files[ptid];
	processes_files_metadata[new_tid] = processes_files_metadata[ptid];
	int ppid = pids[ptid];
	int new_pid = pids[new_tid];
	if (ppid != new_pid) {
	writers[new_tid] = writers[ptid];
	for (auto &p : writers[new_tid]) {
		p.second = false;
	}
	}
	std::unordered_set<std::string> parent_files = get_paths_opened_files(ptid);
	for(std::string path : parent_files) {
		Capio_file& c_file = std::get<4>(files_metadata[path]);
		++c_file.n_opens;
	}
}

off64_t create_dir(int tid, const char* pathname, int rank, bool root_dir) {
	off64_t res;
	#ifdef CAPIOLOG
	logfile << "handle mkdir " << pathname << std::endl;
	#endif
	if (files_metadata.find(pathname) == files_metadata.end()) {
		std::string shm_name = pathname;
		std::replace(shm_name.begin(), shm_name.end(), '/', '_');
		shm_name = shm_name.substr(1);
		void* p_shm = new char[dir_initial_size];
		create_file(pathname, p_shm, true);	
		if (std::get<3>(files_metadata[pathname])) {
			std::get<3>(files_metadata[pathname]) = false;
            write_file_location("files_location.txt", rank, pathname);
			//TODO: it works only if there is one prod per file
			if (!root_dir) {
				update_dir(tid, pathname, rank);
			}
				write_entry_dir(tid, pathname, pathname, 1);
				std::string parent_dir = get_parent_dir_path(pathname);
				write_entry_dir(tid, parent_dir, pathname, 2);
        }
		res = 0;
	}
	else {
		res = 1;
	}
	#ifdef CAPIOLOG
	logfile << "handle mkdir returning " << res << std::endl;
	#endif
	return res;
}

void handle_mkdir(const char* str, int rank) {
	pid_t tid;
	char pathname[PATH_MAX];
	sscanf(str, "mkdi %d %s", &tid, pathname);
	init_process(tid);
	off64_t res = create_dir(tid, pathname, rank, false);	
	response_buffers[tid]->write(&res);

}

void handle_dup(const char* str) {
	int tid;
	int old_fd, new_fd;
	sscanf(str, "dupp %d %d %d", &tid, &old_fd, &new_fd);
	processes_files[tid][new_fd] = processes_files[tid][old_fd];
	std:: string path = processes_files_metadata[tid][old_fd];
	processes_files_metadata[tid][new_fd] = path;
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	++c_file.n_opens;
}


// std::unordered_map<int, std::unordered_map<int, std::string>> processes_files_metadata;
// std::unordered_map<std::string, std::tuple<void*, off64_t*, off64_t, bool, Capio_file>> files_metadata;
// std::unordered_map<int, std::unordered_map<std::string, bool>> writers;

// pathname -> node
//std::unordered_map<std::string, char*> files_location;



//std::unordered_map<std::string, std::vector<std::tuple<int, int, off64_t>>>  pending_reads;
void handle_rename(const char* str, int rank) {
	char oldpath[PATH_MAX];
	char newpath[PATH_MAX];
	int tid;
	off64_t res;
	sscanf(str, "rnam %s %s %d", oldpath, newpath, &tid);
	if (files_metadata.find(oldpath) == files_metadata.end())
		res = 1;
	else 
		res = 0;

	response_buffers[tid]->write(&res);

	if (res == 1) {
		return;
	}

	for (auto& pair : processes_files_metadata) {
		for (auto& pair_2 : pair.second) {
			if (pair_2.second == oldpath) {
				pair_2.second = newpath;
			}
		}
	}

	auto node = files_metadata.extract(oldpath);
	node.key() = newpath;
	files_metadata.insert(std::move(node));

	for (auto& pair : writers) {
		auto node = pair.second.extract(oldpath);
		if (!node.empty()) {
			node.key() = newpath;
			pair.second.insert(std::move(node));
		}
	}


	auto node_2 = files_location.extract(oldpath);
	if (!node_2.empty()) {
		node_2.key() = newpath;
		files_location.insert(std::move(node_2));
	}

	//TODO: streaming + renaming?
	
    write_file_location("files_location.txt", rank, newpath);
	//TODO: remove from files_location oldpath
}


void handle_handshake(const char* str) {
	int tid, pid;
	sscanf(str, "hand %d %d", &tid, &pid);
	pids[tid] = pid;
}


void handle_stat_reply(const char* str) {
	off64_t size;
	int dir_tmp;
	char path_c[1024];
	sscanf(str, "stam %s %ld %d", path_c, &size, &dir_tmp);
	off64_t dir = dir_tmp;
	#ifdef CAPIOLOG
		logfile << "serving the remote stat: " << str << std::endl;
	#endif

	auto it = my_remote_pending_stats.find(path_c);
	if (it == my_remote_pending_stats.end())
		exit(1);
	for (int tid : it->second) {
		response_buffers[tid]->write(&size);
		response_buffers[tid]->write(&dir);
		#ifdef CAPIOLOG
		logfile << "tid: " << tid << std::endl;
		logfile << "file size stat : " << size << std::endl;
		logfile << "file is_dir stat : " << dir << std::endl;
		#endif
	}
	my_remote_pending_stats.erase(it);
	
}

void read_next_msg(int rank) {
	char str[2048];
	std::fill(str, str + 2048, 0);
	#ifdef CAPIOLOG
	logfile << "waiting msg" << std::endl;
	#endif
	buf_requests->read(str);
	char* p = str;
	#ifdef CAPIOLOG
	logfile << "next msg " << str << std::endl;
	#endif
	if (strncmp(str, "hand", 4) == 0)
		handle_handshake(str);
	else if (strncmp(str, "open", 4) == 0)
		handle_open(str, p, rank);
	else if (strncmp(str, "writ", 4) == 0)
		handle_write(str, rank);
	else if (strncmp(str, "read", 4) == 0)
		handle_read(str, rank, false);
	else if (strncmp(str, "clos", 4) == 0)
		handle_close(str, p);
	else if (strncmp(str, "ream", 4) == 0)
		handle_remote_read(str, p, rank);
	else if (strncmp(str, "seek", 4) == 0)
		handle_lseek(str);
	else if (strncmp(str, "sdat", 4) == 0)
		handle_sdat(str);
	else if (strncmp(str, "shol", 4) == 0)
		handle_shol(str);
	else if (strncmp(str, "send", 4) == 0)
		handle_seek_end(str);
	else if (strncmp(str, "exig", 4) == 0)
		handle_exig(str);
	else if (strncmp(str, "stat", 4) == 0)
		handle_stat(str, rank);
	else if (strncmp(str, "stam", 4) == 0)
		handle_stat_reply(str);
	else if (strncmp(str, "fsta", 4) == 0)
		handle_fstat(str, rank);
	else if (strncmp(str, "accs", 4) == 0)
		handle_access(str);
	else if (strncmp(str, "unlk", 4) == 0)
		handle_unlink(str);
	else if (strncmp(str, "clon", 4) == 0)
		handle_clone(str);
	else if (strncmp(str, "mkdi", 4) == 0)
		handle_mkdir(str, rank);
	else if (strncmp(str, "dupp", 4) == 0)
		handle_dup(str);
	else if (strncmp(str, "dent", 4) == 0)
		handle_read(str, rank, true);
	else if (strncmp(str, "rnam", 4) == 0)
		handle_rename(str, rank);
	else {
		logfile << "error msg read" << std::endl;
		MPI_Finalize();
		exit(1);
	}
	return;
}

void clean_files_location() {
	int fd;
	if ((fd = open("files_location.txt", O_CREAT|O_TRUNC, 0664)) == -1) {
		err_exit("open files_location");
	}
	
	if (close(fd) == -1) {
		logfile << "impossible close the file files_location" << std::endl;
	}
}

void handshake_servers(int rank, int size) {
	char* buf;	
	buf = (char*) malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
	if (rank == 0) {
		clean_files_location();
	}
	for (int i = 0; i < size; i += 1) {
		if (i != rank) {
			MPI_Send(node_name, strlen(node_name), MPI_CHAR, i, 0, MPI_COMM_WORLD); //TODO: possible deadlock
			std::fill(buf, buf + MPI_MAX_PROCESSOR_NAME, 0);
			MPI_Recv(buf, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			nodes_helper_rank[buf] = i;
		}
	}
	free(buf);
}

void* capio_server(void* pthread_arg) {
	int rank, size;
	rank = *(int*)pthread_arg;
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	catch_sigterm();
	handshake_servers(rank, size);
	int pid = getpid();
	create_dir(pid, capio_dir->c_str(), rank, true); //TODO: can be a problem if a process execute readdir on capio_dir
	buf_requests = new Circular_buffer<char>("circular_buffer", 1024 * 1024, sizeof(char) * 256);
	sem_post(&internal_server_sem);
	#ifdef CAPIOLOG
	logfile << "capio dir 2 " << *capio_dir << std::endl;
	#endif	
	while(true) {
		read_next_msg(rank);
		#ifdef CAPIOLOG
		logfile << "after next msg " << std::endl;
		#endif

		//respond();
	}
	return nullptr; //pthreads always needs a return value
}

struct remote_read_metadata {
	char path[PATH_MAX];
	long int offset;
	int dest;
	long int nbytes;
	bool* complete;
	sem_t* sem;
};

void send_file(char* shm, long int nbytes, int dest) {
	if (nbytes == 0) {
		#ifdef CAPIOLOG
		logfile << "returning without sending file" << std::endl;
		#endif
		return;
	}
	long int num_elements_to_send = 0;
	MPI_Request req;
	for (long int k = 0; k < nbytes; k += num_elements_to_send) {
			if (nbytes - k > 1024L * 1024 * 1024)
				num_elements_to_send = 1024L * 1024 * 1024;
			else
				num_elements_to_send = nbytes - k;
			//MPI_Isend(shm + k, num_elements_to_send, MPI_BYTE, dest, 0, MPI_COMM_WORLD, &req); 
		#ifdef CAPIOLOG
			logfile << "before sending file k " << k << " num elem to send " << num_elements_to_send << std::endl;
	#endif
			//MPI_Send(shm + k, num_elements_to_send, MPI_CHAR, dest, 0, MPI_COMM_WORLD); 
			MPI_Isend(shm + k, num_elements_to_send, MPI_CHAR, dest, 0, MPI_COMM_WORLD, &req); 
		#ifdef CAPIOLOG
			logfile << "after sending file" << std::endl;
#endif
	}
}

//TODO: refactor offset_str and offset

void serve_remote_read(const char* path_c, int dest, long int offset, long int nbytes, int complete) {
	sem_wait(&remote_read_sem);
	char* buf_send;
	// Send all the rest of the file not only the number of bytes requested
	// Useful for caching
	void* file_shm;
	size_t file_size;
	if (files_metadata.find(path_c) != files_metadata.end()) {
		file_shm = std::get<0>(files_metadata[path_c]);
		file_size = *std::get<1>(files_metadata[path_c]);
		}
	else {
		logfile << "error capio_helper file " << path_c << " not in shared memory" << std::endl;
		exit(1);
	}
	nbytes = file_size - offset;

	if (PREFETCH_DATA_SIZE != 0 && nbytes > PREFETCH_DATA_SIZE)
		nbytes = PREFETCH_DATA_SIZE;

	std::string nbytes_str = std::to_string(nbytes);
	const char* nbytes_cstr = nbytes_str.c_str();
	std::string offset_str = std::to_string(offset);
	const char* offset_cstr = offset_str.c_str();
	std::string complete_str = std::to_string(complete);
	const char* complete_cstr = complete_str.c_str();
	std::string file_size_str = std::to_string(file_size);
	const char* file_size_cstr = file_size_str.c_str();
	const char* s1 = "sending";
	const size_t len1 = strlen(s1);
	const size_t len2 = strlen(path_c);
	const size_t len3 = strlen(offset_cstr);
	const size_t len4 = strlen(nbytes_cstr);
	const size_t len5 = strlen(complete_cstr);
	const size_t len6 = strlen(file_size_cstr);
	buf_send = (char*) malloc((len1 + len2 + len3 + len4 + len5 + len6 + 6) * sizeof(char));//TODO:add malloc check
	sprintf(buf_send, "%s %s %s %s %s %s", s1, path_c, offset_cstr, nbytes_cstr, complete_cstr, file_size_cstr);
	#ifdef CAPIOLOG
		logfile << "helper serve remote read msg sent: " << buf_send << " to " << dest << std::endl;
	#endif
	//send warning
	MPI_Send(buf_send, strlen(buf_send) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
	free(buf_send);
	//send data
	#ifdef MYDEBUG
	int* tmp = (int*) malloc(*size_shm);
	logfile << "helper sending " << *size_shm << " bytes" << std::endl;
	memcpy(tmp, file_shm, *size_shm); 
	for (int i = 0; i < *size_shm / sizeof(int); ++i) {
		logfile << "helper sending tmp[i] " << tmp[i] << std::endl;
	}
	free(tmp);
	#endif
	#ifdef CAPIOLOG
		logfile << "before sending part of the file to : " << dest << " with offset " << offset << " nbytes" << nbytes << std::endl;
	#endif
	send_file(((char*) file_shm) + offset, nbytes, dest);
	#ifdef CAPIOLOG
		logfile << "after sending part of the file to : " << dest << std::endl;
	#endif
	sem_post(&remote_read_sem);
}

void* wait_for_data(void* pthread_arg) {
	struct remote_read_metadata* rr_metadata = (struct remote_read_metadata*) pthread_arg;
	const char* path = rr_metadata->path;
	long int offset = rr_metadata->offset;
	int dest = rr_metadata->dest;
	long int nbytes = rr_metadata->nbytes;
	sem_wait(rr_metadata->sem);
	bool complete = *rr_metadata->complete; //warning: possible data races
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	complete = c_file.complete;
	serve_remote_read(path, dest, offset, nbytes, complete);
	free(rr_metadata->sem);
	//free(rr_metadata->path);
	free(rr_metadata);
	return nullptr;
}


bool data_avaiable(const char* path_c, long int offset, long int nbytes_requested, long int file_size) {
	return (offset + nbytes_requested <= file_size);
}

void lightweight_MPI_Recv(char* buf_recv, size_t buf_size, MPI_Status* status) {
	MPI_Request request;
	int received = 0;
	MPI_Irecv(buf_recv, buf_size, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &request); //receive from server
	struct timespec sleepTime;
    struct timespec returnTime;
    sleepTime.tv_sec = 0;
    sleepTime.tv_nsec = 200000;

	while (!received) {
		MPI_Test(&request, &received, status);
		nanosleep(&sleepTime, &returnTime);
	}
}

void recv_file(char* shm, int source, long int bytes_expected) {
	MPI_Status status;
	int bytes_received;
	int count;
	for (long int k = 0; k < bytes_expected; k += bytes_received) {
		if (bytes_expected - k > 1024L * 1024 * 1024)
			count = 1024L * 1024 * 1024;
		else
			count = bytes_expected - k;
    #ifdef CAPIOLOG
		logfile << "debug0 count " << count << " k " << k << std::endl;
#endif
		MPI_Recv(shm + k, count, MPI_CHAR, source, 0, MPI_COMM_WORLD, &status);
    #ifdef CAPIOLOG
		logfile << "debug1" << std::endl;
#endif
		MPI_Get_count(&status, MPI_CHAR, &bytes_received);
    #ifdef CAPIOLOG
		logfile << "recv_file bytes_received " << bytes_received << std::endl;
#endif
	}
}

struct remote_stat_metadata {
	char* path;
	Capio_file* c_file;
	int dest;
	sem_t* sem;
};

void serve_remote_stat(const char* path, int dest, Capio_file& c_file) {
	char msg[PATH_MAX + 1024];	
	int dir;
	#ifdef CAPIOLOG
		logfile << "serve remote stat" << std::endl;
	#endif
	if (c_file.is_dir())
		dir = 0;
	else
		dir = 1;
	off64_t size = c_file.get_file_size();
	sprintf(msg, "size %s %ld %d", path, size, dir);
	#ifdef CAPIOLOG
		logfile << "serve remote stat 0" << std::endl;
	#endif
	MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
	#ifdef CAPIOLOG
		logfile << "serve remote stat 1" << std::endl;
	#endif
}

void* wait_for_completion(void* pthread_arg) {
	struct remote_stat_metadata* rr_metadata = (struct remote_stat_metadata*) pthread_arg;
	const char* path = rr_metadata->path;
	Capio_file* c_file = rr_metadata->c_file;
			#ifdef CAPIOLOG
				logfile << "wait for completion before" << std::endl;
			#endif
	sem_wait(rr_metadata->sem);
	serve_remote_stat(path, rr_metadata->dest, *c_file);
	free(rr_metadata->sem);
//	free(rr_metadata->path);
	free(rr_metadata);
			#ifdef CAPIOLOG
				logfile << "wait for completion after" << std::endl;
			#endif
	return nullptr;
}

void helper_stat_req(const char* buf_recv) {
	char* path_c = (char*) malloc(sizeof(char) * PATH_MAX);
	int dest;
	#ifdef CAPIOLOG
	logfile << "helper stat req" << std::endl;
	#endif
	sscanf(buf_recv, "stat %d %s", &dest, path_c);

	//std::string path(path_c);
	#ifdef CAPIOLOG
	logfile << "debug 0 path " << path_c << "len " << strlen(path_c) << " " << dest << std::endl;
	logfile << "elems " << files_metadata.size() << std::endl;
	#endif
	//for (auto p : files_metadata) {
//		logfile << "files metadata " << p.first << std::endl;
//	}
	Capio_file& c_file = std::get<4>(files_metadata[path_c]);	
	#ifdef CAPIOLOG
	logfile << "debug 000" << std::endl;
	#endif
	if (c_file.complete) {
	#ifdef CAPIOLOG
	logfile << "debug 1" << std::endl;
	#endif
		serve_remote_stat(path_c, dest, c_file);
	}
	else { //wait for completion
	#ifdef CAPIOLOG
	logfile << "debug 2" << std::endl;
	#endif
				pthread_t t;
				struct remote_stat_metadata* rr_metadata = (struct remote_stat_metadata*) malloc(sizeof(struct remote_stat_metadata));
//				rr_metadata->path = path_c;
				strcpy(rr_metadata->path, path_c);
				rr_metadata->c_file = &c_file;
				rr_metadata->dest = dest;
				rr_metadata->sem = (sem_t*) malloc(sizeof(sem_t));
				int res = sem_init(rr_metadata->sem, 0, 0);
				if (res !=0) {
					logfile << __FILE__ << ":" << __LINE__ << " - " << std::flush;
					perror("sem_init failed"); 
					exit(1);
				}
				res = pthread_create(&t, NULL, wait_for_completion, (void*) rr_metadata);
				if (res != 0) {
					logfile << "error creation of capio server thread wait for completion" << std::endl;
					MPI_Finalize();
					exit(1);
				}
				clients_remote_pending_stat[path_c].push_back(rr_metadata->sem);
	}
    #ifdef CAPIOLOG
	logfile << "debug 3" << std::endl;
#endif
	free(path_c);
}

void helper_handle_stat_reply(char* buf_recv) {
	#ifdef CAPIOLOG
	logfile << "helper received size msg" << std::endl;
	#endif
	char path_c[1024];
	off64_t size;
	int dir;
	sscanf(buf_recv, "size %s %ld %d", path_c, &size, &dir);
	std::string path(path_c);
	std::string msg = "stam " + path + " " + std::to_string(size) + " " + std::to_string(dir);
	const char* c_str = msg.c_str();
	buf_requests->write(c_str, 256 * sizeof(char));
}

void* capio_helper(void* pthread_arg) {
	size_t buf_size = sizeof(char) * (PATH_MAX + 1024);
	char* buf_recv = (char*) malloc(buf_size);
	MPI_Status status;
	int rank = *(int*) pthread_arg;
	sem_wait(&internal_server_sem);
	while(true) {
		#ifdef CAPIOSYNC
		MPI_Recv(buf_recv, buf_size, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status); //receive from server
		#else
		lightweight_MPI_Recv(buf_recv, buf_size, &status); //receive from server
		#endif

		bool remote_request_to_read = strncmp(buf_recv, "read", 4) == 0;
		if (remote_request_to_read) {
		#ifdef CAPIOLOG
		logfile << "helper remote req to read " << buf_recv << std::endl;
		#endif
		    // schema msg received: "read path dest offset nbytes"
			// TODO: use sscanf
			char* path_c = (char*) malloc(sizeof(char) * PATH_MAX);
			int dest;
			long int offset, nbytes;
    #ifdef CAPIOLOG
			logfile << "debug -1 " << std::endl;
#endif
			sscanf(buf_recv, "read %s %d %ld %ld", path_c, &dest, &offset, &nbytes);
    #ifdef CAPIOLOG
			logfile << "debug 0 " << std::endl;
#endif
			
			//check if the data is avaiable
			auto it = files_metadata.find(path_c);
			long int file_size = *std::get<1>(it->second);
			Capio_file& c_file = std::get<4>(it->second);
			bool complete = c_file.complete; 
			if (complete || (c_file.get_mode() == "append" && data_avaiable(path_c, offset, nbytes, file_size))) {
				#ifdef CAPIOLOG
					logfile << "helper data avaiable" << std::endl;
				#endif
				serve_remote_read(path_c, dest, offset, nbytes, complete);
			}
			else {
				#ifdef CAPIOLOG
					logfile << "helper data not avaiable" << std::endl;
				#endif
				pthread_t t;
				struct remote_read_metadata* rr_metadata = (struct remote_read_metadata*) malloc(sizeof(struct remote_read_metadata));
//				rr_metadata->path = path_c;
				strcpy(rr_metadata->path, path_c);
				rr_metadata->offset = offset;
				rr_metadata->dest = dest;
				rr_metadata->nbytes = nbytes;
				rr_metadata->complete = &(c_file.complete);
				rr_metadata->sem = (sem_t*) malloc(sizeof(sem_t));
				int res = sem_init(rr_metadata->sem, 0, 0);
				if (res !=0) {
					logfile << __FILE__ << ":" << __LINE__ << " - " << std::flush;
					perror("sem_init failed"); 
					exit(1);
				}
				res = pthread_create(&t, NULL, wait_for_data, (void*) rr_metadata);
				if (res != 0) {
					logfile << "error creation of capio server thread" << std::endl;
					MPI_Finalize();
					exit(1);
				}
				clients_remote_pending_reads[path_c].push_back(std::make_tuple(offset, nbytes, rr_metadata->sem));
			}
			free(path_c);
		}
		else if(strncmp(buf_recv, "sending", 7) == 0) { //receiving a file
			#ifdef CAPIOLOG
				logfile << "helper received sending msg" << std::endl;
			#endif
			off64_t bytes_received;
			int source = status.MPI_SOURCE;
			off64_t offset; 
			char path_c[1024];
			int complete_tmp;
			size_t file_size;
			sscanf(buf_recv, "sending %s %ld %ld %d %zu", path_c, &offset, &bytes_received, &complete_tmp, &file_size);
			bool complete = complete_tmp;
			std::string path(path_c);
			void* file_shm; 
			if (files_metadata.find(path) != files_metadata.end()) {
				file_shm = std::get<0>(files_metadata[path]);
			}
			else {
				logfile << "error capio_helper file " << path << " not in shared memory" << std::endl;
				exit(1);
			}
			#ifdef CAPIOLOG
				logfile << "helper before received part of the file from process " << source << std::endl;
				logfile << "offset " << offset << std::endl;
				logfile << "bytes received " << bytes_received << std::endl;
			#endif
			if (bytes_received != 0) {
				recv_file((char*)file_shm + offset, source, bytes_received);
			#ifdef CAPIOLOG
				logfile << "helper received part of the file" << std::endl;
			#endif
				bytes_received *= sizeof(char);
			}
			std::string msg = "ream " + path + + " " + std::to_string(bytes_received) + " " + std::to_string(offset) + " " + std::to_string(complete) + std::to_string(file_size);
			const char* c_str = msg.c_str();
			buf_requests->write(c_str, 256 * sizeof(char));
		}
		else if(strncmp(buf_recv, "stat", 4) == 0) {
			helper_stat_req(buf_recv);
    #ifdef CAPIOLOG
		logfile << "helper loop -1" << std::endl;
#endif
		}
		else if (strncmp(buf_recv, "size", 4) == 0) {
			helper_handle_stat_reply(buf_recv);
		}
		else {
			logfile << "helper error receiving message" << std::endl;
		}
    #ifdef CAPIOLOG
		logfile << "helper loop" << std::endl;
#endif
	}
	return nullptr; //pthreads always needs a return value
}

void parse_conf_file(std::string conf_file) {
	ondemand::parser parser;
	padded_string json;
	try {
		json = padded_string::load(conf_file);
	}
	catch (const simdjson_error& e) {
		logfile << "Exception thrown while opening conf file: " << e.what() << std::endl;
		exit(1);
	}
	ondemand::document entries = parser.iterate(json);
	auto workflow_name = entries["name"];	
	#ifdef CAPIOLOG
	logfile << "workflow name: " << workflow_name << std::endl;
	#endif
	auto io_graph = entries["IO_Graph"];
	for (auto app : io_graph) {
		std::string_view app_name = app["name"].get_string();
		#ifdef CAPIOLOG
		logfile << "app name: " << app_name << std::endl;
		#endif
		ondemand::array input_stream;
		auto error = app["input_stream"].get_array().get(input_stream);
		if (!error) {
			for (auto group : input_stream) {
				std::string_view group_name;
				error = group["group_name"].get_string().get(group_name);;
				if (!error) {
					#ifdef CAPIOLOG
					logfile << "group name " << group_name << std::endl;
					#endif
					auto files = group["files"];
					for (auto file : files) {
						#ifdef CAPIOLOG
						logfile << "file: " << file << std::endl; 
						#endif
					}
				}
				else {
					#ifdef CAPIOLOG
					logfile << "simple file" << group << std::endl;
					#endif
				}
			}
		}
		ondemand::array output_stream;
		error = app["output_stream"].get_array().get(output_stream);
		if (!error) {
			for (auto group : output_stream) {
				std::string_view group_name;
				error = group["group_name"].get_string().get(group_name);;
				if (!error) {
					#ifdef CAPIOLOG
					logfile << "group name " << group_name << std::endl;
					#endif
					auto files = group["files"];
					for (auto file : files) {
						#ifdef CAPIOLOG
						logfile << "file: " << file << std::endl; 
						#endif
					}
				}
				else {
					#ifdef CAPIOLOG
					logfile << "simple file" << group << std::endl;
					#endif
				}
			}
		}
		ondemand::array streaming;
		error = app["streaming"].get_array().get(streaming);
		if (!error) {
			for (auto file : streaming) {
				std::string_view name;
				error = file["name"].get_string().get(name);
				if (!error) {
					#ifdef CAPIOLOG
					logfile << " name " << name << std::endl;
					#endif
				}
				std::string_view committed;
				error = file["committed"].get_string().get(committed);
				if (!error) {
					#ifdef CAPIOLOG
					logfile << " committed " << committed << std::endl;
					#endif
				}
				std::string_view mode;
				error = file["mode"].get_string().get(mode);
				if (!error) {
					#ifdef CAPIOLOG
					logfile << " mode " << mode << std::endl;
					#endif
				}
				std::string path = std::string(name);
				if (!is_absolute(path.c_str())) {
					if (path.substr(0, 2) == "./") 
						path = path.substr(2, path.length() - 2);
					path = *capio_dir + "/" + path;
				}
				#ifdef CAPIOLOG
				logfile << "conf path " << path << std::endl;
				#endif
				metadata_conf[path] = std::make_tuple(std::string(committed), std::string(mode));
			}
		}
	}

	ondemand::array permanent_files;
	auto error = entries["permanent"].get_array().get(permanent_files);
	if (!error) {
		for (auto file : permanent_files) {
			#ifdef CAPIOLOG
			logfile << "permament file: " << file << std::endl;
			#endif
		}
	}

}

void get_capio_dir() {
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
		logfile << "dir " << capio_dir << " is not a directory" << std::endl;
		exit(1);
	}
	}
	#ifdef CAPIOLOG
	logfile << "capio dir " << *capio_dir << std::endl;
	#endif	

}

void get_prefetch_data_size() {
	char* val;
	val = getenv("CAPIO_PREFETCH_DATA_SIZE");
	if (val != NULL) {
		PREFETCH_DATA_SIZE = strtol(val, NULL, 10);
	}
	#ifdef CAPIOLOG
	logfile << "PREFETCH_DATA_SIZE " << PREFETCH_DATA_SIZE << std::endl;
	#endif	
}

int main(int argc, char** argv) {
	int rank, len, provided;
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	std::string conf_file;
	if (argc < 2 || argc > 3) {
		logfile << "input error: ./capio_server server_log_path [conf_file]" << std::endl;
		exit(1);
	}
	std::string server_log_path = argv[1];
  	logfile.open (server_log_path + "_" + std::to_string(rank), std::ofstream::out);
	get_capio_dir();
	get_prefetch_data_size();
	if (argc == 3) {
		conf_file = argv[2];
		parse_conf_file(conf_file);
	}
	sems_write = new std::unordered_map<int, sem_t*>;
    if(provided != MPI_THREAD_MULTIPLE)
    {
        logfile << "The threading support level is lesser than that demanded" << std::endl;
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
	MPI_Get_processor_name(node_name, &len);
	pthread_t server_thread, helper_thread;
	int res = sem_init(&internal_server_sem, 0, 0);
	if (res != 0) {
		logfile << __FILE__ << ":" << __LINE__ << " - " << std::flush;
		perror("sem_init failed"); exit(res);
	}
	sem_init(&remote_read_sem, 0, 1);
	sem_init(&handle_remote_read_sem, 0, 1);
	sem_init(&handle_remote_stat_sem, 0, 1);
	sem_init(&handle_local_read_sem, 0, 1);
	if (res !=0) {
		logfile << __FILE__ << ":" << __LINE__ << " - " << std::flush;
		perror("sem_init failed"); exit(res);
	}
	res = pthread_create(&server_thread, NULL, capio_server, &rank);
	if (res != 0) {
		logfile << "error creation of capio server thread" << std::endl;
		MPI_Finalize();
		return 1;
	}
	res = pthread_create(&helper_thread, NULL, capio_helper, &rank);
	if (res != 0) {
		logfile << "error creation of helper server thread" << std::endl;
		MPI_Finalize();
		return 1;
	}
    void* status;
    int t = pthread_join(server_thread, &status);
    if (t != 0) {
    	logfile << "Error in thread join: " << t << std::endl;
    }
    t = pthread_join(helper_thread, &status);
    if (t != 0) {
    	logfile << "Error in thread join: " << t << std::endl;
    }
	res = sem_destroy(&internal_server_sem);
	if (res !=0) {
		logfile << __FILE__ << ":" << __LINE__ << " - " << std::flush;
		perror("sem_destroy failed"); exit(res);
	}
	res = sem_destroy(&remote_read_sem);
	if (res !=0) {
		logfile << __FILE__ << ":" << __LINE__ << " - " << std::flush;
		perror("sem_destroy failed"); exit(res);
	}
	MPI_Finalize();
	return 0;
}
