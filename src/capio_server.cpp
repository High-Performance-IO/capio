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
#include <cstdio>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <csignal>
#include <dirent.h> /* Defines DT_* constants */

#include <mpi.h>

#include "capio_file.hpp"
#include "spsc_queue.hpp"
#include "simdjson.h"
#include "utils/common.hpp"

using namespace simdjson;

std::ofstream logfile;

std::string* capio_dir = nullptr;

/* constants definition start */

// initial size for each file (can be overwritten by the user)
size_t file_initial_size = 1024L * 1024 * 1024 * 4;

const size_t dir_initial_size = 1024L * 1024 * 1024;

/* constants definition ending */

off64_t PREFETCH_DATA_SIZE = 0;

MPI_Request req;
int n_servers;
int fd_files_location;

// [(fd, fp, bool), ....] the third argument is true if the last time getline returned -1, false otherwise
std::vector<std::tuple<int, FILE*, bool>> fd_files_location_reads;

/*
 * For multithreading:
 * tid -> pid
 */

std::unordered_map<int, int> pids;

// tid -> application name 
std::unordered_map<int, std::string> apps;

// application name -> set of files already sent
std::unordered_map<std::string, std::unordered_set<std::string>> files_sent;

// tid -> fd ->(capio_file, index)
std::unordered_map<int, std::unordered_map<int, std::tuple<Capio_file*, off64_t*>>> processes_files;

// tid -> fd -> pathname
std::unordered_map<int, std::unordered_map<int, std::string>> processes_files_metadata;

// tid -> (response shared buffer, index)
std::unordered_map<int, Circular_buffer<off_t>*> response_buffers;

// tid -> (client_to_server_data_buf, server_to_client_data_buf)
std::unordered_map<int, std::pair<SPSC_queue<char>*, SPSC_queue<char>*>> data_buffers;

/* pathname ->  capio_file
 */

std::unordered_map<std::string, Capio_file*> files_metadata;

// path -> (committed, mode, app_name, n_files, bool, n_close)

std::unordered_map<std::string, std::tuple<std::string, std::string, std::string, long int, bool, long int>> metadata_conf;

// [(glob, committed, mode, app_name, n_files, batch_size, permanent, n_close), (glob, committed, mode, app_name, n_files, batch_size, permanent, n_close), ...]

std::vector<std::tuple<std::string, std::string, std::string, std::string, long int, long int, bool, long int>> metadata_conf_globs;

/*
 * pid -> pathname -> bool
 * Different threads with the same pid are threated as a single writer
 */

std::unordered_map<int, std::unordered_map<std::string, bool>> writers;

// pathname -> (node, offset)
std::unordered_map<std::string, std::pair<char*, long int>> files_location;

// pathname -> node
std::unordered_map<std::string, std::string> static_home_nodes;

// node -> rank
std::unordered_map<std::string, int> nodes_helper_rank;
//rank -> node
std::unordered_map<int, std::string> rank_to_node;

/*
 * It contains all the reads requested by local processes to read files that are in the local node for which the data is not yet avaiable.
 * path -> [(tid, fd, numbytes, is_getdents), ...]
 */

std::unordered_map<std::string, std::vector<std::tuple<int, int, off64_t, bool>>>  pending_reads;

/*
 * It contains all the reads requested to the remote nodes that are not yet satisfied 
 * path -> [(tid, fd, numbytes, is_getdents), ...]
 */

std::unordered_map<std::string, std::list<std::tuple<int, int, off64_t, bool>>>  my_remote_pending_reads;

/*
 * It contains all the stats requested to the remote nodes that are not yet satisfied 
 * path -> [tid1, tid2, tid3, ...]
 */

std::unordered_map<std::string, std::list<int>>  my_remote_pending_stats;

/*
 * It contains all the read requested by other nodes for which the data is not yet avaiable 
 * path -> [(offset, numbytes, sem_pointer), ...]
 */

std::unordered_map<std::string, std::list<std::tuple<size_t, size_t, sem_t*>>> clients_remote_pending_reads;

std::unordered_map<std::string, std::list<sem_t*>> clients_remote_pending_stat;

struct remote_n_files {
	char* prefix;
	std::size_t n_files;
	int dest;
	std::vector<std::string>* files_path;
	sem_t* sem;

};

std::unordered_map<std::string, std::list<struct remote_n_files*>> clients_remote_pending_nfiles;

// name of the node

char node_name[MPI_MAX_PROCESSOR_NAME];

Circular_buffer<char>* buf_requests; 
//std::unordered_map<int, sem_t*> sems_response;
std::unordered_map<int, sem_t*>* sems_write;

sem_t internal_server_sem;
sem_t remote_read_sem;
sem_t handle_remote_read_sem;
sem_t handle_remote_stat_sem;
sem_t handle_local_read_sem;
sem_t files_metadata_sem;
sem_t files_location_sem;
sem_t clients_remote_pending_nfiles_sem;

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
    #ifdef CAPIOLOG
    logfile << "sigterm captured, freeing resources..." << std::endl;
    #endif
	//free all the memory used
	if (sem_wait(&files_metadata_sem) == -1)
		err_exit("sem_wait files_metadata_sem in sig_term_handler", logfile);
	if (sem_post(&files_metadata_sem) == -1)
		err_exit("sem_post files_metadata_sem in sig_term_handler", logfile);

	std::string offset_shm_name;
	for (auto& p1 : processes_files) {
		for(auto& p2 : p1.second) {
			offset_shm_name = "offset_" + std::to_string(p1.first) +  "_" + std::to_string(p2.first);
			shm_unlink(offset_shm_name.c_str()); // no check because offset could not exist for dup
		}
	}

	std::string sem_write_shm_name;
	for (auto& pair : response_buffers) {
		pair.second->free_shm();
		delete pair.second;
		sem_write_shm_name = "sem_write" + std::to_string(pair.first);
		if (sem_unlink(sem_write_shm_name.c_str()) == -1)
			err_exit("sem_unlink " + sem_write_shm_name + "in sig_term_handler", logfile);
	}

	for (auto& p : data_buffers) {
		p.second.first->free_shm();
		p.second.second->free_shm();
	}

	buf_requests->free_shm(logfile);

    #ifdef CAPIOLOG
    logfile << "server terminated" << std::endl;
    #endif
	MPI_Finalize();
	exit(0);
}

void catch_sigterm() {
    static struct sigaction sigact;
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_sigaction = sig_term_handler;
	sigact.sa_flags = SA_SIGINFO;
	int res = sigaction(SIGTERM, &sigact, nullptr);
	if (res == -1) {
		err_exit("sigaction for SIGTERM", logfile);
	}
}

void write_file_location(int rank, const std::string& path_to_write, int tid) {
    #ifdef CAPIOLOG
    logfile << "write file location before, tid " << tid << std::endl;
    #endif
    struct flock lock;
    memset(&lock, 0, sizeof(lock));

    int fd = fd_files_location;
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

	long offset = lseek(fd, 0, SEEK_CUR);
	if (offset == -1)
		err_exit("lseek in write_file_location", logfile);

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
    	logfile << "file_location writing " << path_to_write << " " << file_location << std::endl;
    #endif
	files_location[path_to_write] = std::make_pair(node_name, offset);
	// Now release the lock explicitly.
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLKW, &lock) == - 1) {
        logfile << "write " << rank << "failed to unlock the file" << std::endl;
    }

	free(file_location);
    //close(fd); // close the file: would unlock if needed
    #ifdef CAPIOLOG
    logfile << "write file location after" << std::endl;
    #endif
}

/*
 * Returns 0 if the file "file_location" does not exist
 * Returns 1 if the location of the file path_to_check is found
 * Returns 2 otherwise
 *
*/

int check_file_location(std::size_t index, int rank, const std::string& path_to_check) {
    FILE * fp;
	bool seek_needed;
    char * line = nullptr;
    size_t len = 0;
    ssize_t read;
    int res = 1;
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;    /* shared lock for read*/
    lock.l_whence = SEEK_SET; /* base for seek offsets */
    lock.l_start = 0;         /* 1st byte in file */
    lock.l_len = 0;           /* 0 here means 'until EOF' */
    lock.l_pid = getpid();    /* process id */

    int fd; /* file descriptor to identify a file within a process */
        #ifdef CAPIOLOG
        logfile << "checking file location read " << index << std::endl;
        #endif
	if (index < fd_files_location_reads.size()) {
		fd = std::get<0>(fd_files_location_reads[index]);
		fp = std::get<1>(fd_files_location_reads[index]);
		seek_needed = std::get<2>(fd_files_location_reads[index]);

        #ifdef CAPIOLOG
        logfile << "file location already opened, index" << index << std::endl;
        #endif
	}
	else {
        #ifdef CAPIOLOG
        logfile << "file location opened, index" << index << std::endl;
        #endif
		std::string index_str = std::to_string(index);
		std::string file_name = "files_location_" + index_str + ".txt";
    	fp = fopen(file_name.c_str(), "r+");
		if (!fp) {
			return 0;
		}

		fd = fileno(fp);
		if (fd == -1)
			err_exit("fileno in check_file_location", logfile);
		seek_needed = false;
		fd_files_location_reads.emplace_back(fd, fp, seek_needed);
	}
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        logfile << "capio server " << rank << " failed to lock the file" << std::endl;
        close(fd);
        exit(1);
    }
	const char* path_to_check_cstr = path_to_check.c_str();
	bool found = false;

	if (seek_needed) {
		long offset = ftell(fp);
		if (fseek(fp, offset, SEEK_SET) == -1)
			err_exit("fseek in check_file_location", logfile);
	}

    while (!found && (read = getline(&line, &len, fp)) != -1) {

		if (line[0] == '0')
			continue;
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
		char* p_node_str = (char*) malloc(sizeof(char) * (strlen(node_str) + 1));
		strcpy(p_node_str, node_str);
		long offset = ftell(fp);
		if (offset == -1)
			err_exit("ftell in check_file_location", logfile);
		if (sem_wait(&files_location_sem) == -1)
			err_exit("sem_wait files_location_sem in check_file_location", logfile);
		files_location[path] = std::make_pair(p_node_str, offset); 
		if (sem_post(&files_location_sem) == -1)
			err_exit("sem_post files_location_sem in check_file_location", logfile);

		if (strcmp(path, path_to_check_cstr) == 0) {
    #ifdef CAPIOLOG
    	logfile << "check remote found" << std::endl;
    #endif
			found = true;
		}
		//check if the file is present
    }
	if (found)
		res = 1;
	else {
		std::get<2>(fd_files_location_reads[index]) = true;
		res = 2;
	}
    /* Release the lock explicitly. */
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
		err_exit("reader " + std::to_string(rank) + " failed to unlock the file", logfile);
    }
    return res;
}

bool check_file_location(int my_rank, const std::string& path_to_check) {
	bool found = false;
	int rank = 0, res = -1;
	while (!found && rank < n_servers) {
		std::string rank_str = std::to_string(rank);
		res = check_file_location(rank, my_rank, path_to_check);
		found = res == 1;
		++rank;
	}
	return found;
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
			err_exit("error creating sem_write" + std::to_string(tid), logfile);
		}
		auto* cb = new Circular_buffer<off_t>("buf_response" + std::to_string(tid), 8 * 1024 * 1024, sizeof(off_t));
		response_buffers.insert({tid, cb});
		std::string shm_name = "capio_write_data_buffer_tid_" + std::to_string(tid);
		auto* write_data_cb = new SPSC_queue<char>(shm_name, *N_ELEMS_DATA_BUFS, *WINDOW_DATA_BUFS);
		shm_name = "capio_read_data_buffer_tid_" + std::to_string(tid);
		auto* read_data_cb = new SPSC_queue<char>(shm_name, *N_ELEMS_DATA_BUFS, *WINDOW_DATA_BUFS);
		data_buffers.insert({tid, {write_data_cb, read_data_cb}});
		//caching_info[tid].first = (int*) get_shm("caching_info" + std::to_string(tid));
		//caching_info[tid].second = (int*) get_shm("caching_info_size" + std::to_string(tid));
		
	}
	#ifdef CAPIOLOG
	logfile << "end init process tid " << std::to_string(tid) << std::endl;
	#endif	

}

long int match_globs(const std::string& path) {
	long int res = -1;
	size_t i = 0;
	size_t max_length_prefix = 0;
	while (i < metadata_conf_globs.size()) {
		std::string prefix_str = std::get<0>(metadata_conf_globs[i]);
		size_t prefix_length = prefix_str.length();
		if (path.compare(0, prefix_length, prefix_str) == 0 && prefix_length > max_length_prefix) {
			res = i;
			max_length_prefix = prefix_length;
		}
		++i;
	}
	return res;
}

bool in_dir(const std::string& path, const std::string& glob) {
		size_t res = path.find('/', glob.length() - 1);
		return res != std::string::npos;
}

void create_file(const std::string& path, bool is_dir, off64_t init_size) {
	std::string shm_name = path;
	std::replace(shm_name.begin(), shm_name.end(), '/', '_');
	shm_name = shm_name.substr(1);
	std::string committed, mode, app_name;
	long int n_files;
	auto it = metadata_conf.find(path);
	Capio_file* p_capio_file;
	if (it == metadata_conf.end()) {
		long int pos = match_globs(path);
		if (pos == -1) {
        	#ifdef CAPIOLOG
			logfile << "creating file without conf file " << path << std::endl;
			#endif
			if (is_dir) {
				init_size = dir_initial_size;
			}
			#ifdef CAPIOLOG
			logfile << "init size " << init_size << std::endl;
			#endif
			if (sem_wait(&files_metadata_sem) == -1)
				err_exit("sem_wait 1 files_metadata_sem in create_file", logfile);
			p_capio_file = new Capio_file(is_dir, false, init_size, logfile);
			files_metadata[path] = p_capio_file;
			if (sem_post(&files_metadata_sem) == -1)
				err_exit("sem_post 1 files_metadata_sem in create_file", logfile);
		}
		else {
			auto& quintuple = metadata_conf_globs[pos];
			std::string glob = std::get<0>(quintuple);
			committed = std::get<1>(quintuple);
			mode = std::get<2>(quintuple);
			app_name = std::get<3>(quintuple);
			n_files = std::get<4>(quintuple);
        	#ifdef CAPIOLOG
			logfile << "creating file using globbing " << path << std::endl;
			logfile << "committed " << committed << " mode " << mode << "app name " << app_name << " nfiles " << n_files <<  std::endl;
			#endif
			if (in_dir(path, glob)) {
				n_files = 0;
			}

			if (n_files > 0) {
				init_size = dir_initial_size;
				is_dir = true;
			}

			if (sem_wait(&files_metadata_sem) == -1)
				err_exit("sem_wait 2 files_metadata_sem in create_file", logfile);
			bool permanent = std::get<6>(metadata_conf_globs[pos]);
			long int n_close = std::get<7>(metadata_conf_globs[pos]);
        	#ifdef CAPIOLOG
			logfile << "creating file " << path << " permanent " << permanent << " dir " << is_dir << std::endl;
			#endif
			p_capio_file = new Capio_file(committed, mode, is_dir, n_files, permanent, init_size, logfile, n_close);
			files_metadata[path] = p_capio_file;
			if (sem_post(&files_metadata_sem) == -1)
				err_exit("sem_post 2 files_metadata_sem in create_file", logfile);
			metadata_conf[path] = std::make_tuple(committed, mode, app_name, n_files, permanent, n_close);
		}
	}
	else {
		committed = std::get<0>(it->second);
		mode = std::get<1>(it->second);
		n_files = std::get<3>(it->second);
        #ifdef CAPIOLOG
		logfile << "creating file " << path << std::endl;
		logfile << "committed " << committed << " mode " << mode << " n_files " << n_files << std::endl;
		#endif
		if (n_files > 0) {
			is_dir = true;
			init_size = dir_initial_size;
		}

		if (sem_wait(&files_metadata_sem) == -1)
			err_exit("sem_wait 3 files_metadata_sem in create_file", logfile);
		bool permanent =  std::get<4>(it->second);
		long int n_close = std::get<5>(it->second);
        	#ifdef CAPIOLOG
			logfile << "creating file " << path << " permanent " << permanent << " dir " << is_dir << std::endl;
			#endif
		p_capio_file = new Capio_file(committed, mode, is_dir, n_files, permanent, init_size, logfile, n_close);
		files_metadata[path] = p_capio_file;
		if (sem_post(&files_metadata_sem) == -1)
			err_exit("sem_post 3 files_metadata_sem in create_file", logfile);
	}
}

std::string get_parent_dir_path(const std::string& file_path) {
	std::size_t i = file_path.rfind('/');
	if (i == std::string::npos) {
		logfile << "invalid file_path in get_parent_dir_path" << std::endl;
	}
	return file_path.substr(0, i);
}

void reply_remote_stats(const std::string& path) {
    #ifdef CAPIOLOG
	logfile << "check reply remote stats " << path << std::endl;
	#endif
	auto it_client = clients_remote_pending_stat.find(path);
    if (it_client !=  clients_remote_pending_stat.end()) {
		for (sem_t* sem : it_client->second) {
			if (sem_post(sem)  == -1)
				err_exit("error sem_post sem in reply_remote_stats", logfile);
    		#ifdef CAPIOLOG
			logfile << "reply remote stat" << std::endl;
			#endif
		}
		clients_remote_pending_stat.erase(path);
	}
}

void handle_pending_remote_reads(const std::string& path, off64_t data_size, bool complete) {
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
            if (complete || (offset + nbytes < data_size)) {
                if (sem_post(sem) == -1)
                    err_exit("sem_post sem in handle_pending_remote_reads", logfile);
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
}


char* expand_memory_for_file(off64_t data_size, Capio_file& c_file) {
	#ifdef CAPIOLOG
	logfile << "expanded memory for file data_size " << data_size << std::endl;
	#endif	
	char* new_p = c_file.expand_buffer(data_size);
	return new_p;
}

void send_data_to_client(int tid, char* buf, long int count) {
	auto* data_buf = data_buffers[tid].second;
	size_t n_writes = count / *WINDOW_DATA_BUFS;
	size_t r = count % *WINDOW_DATA_BUFS;
	size_t i = 0;
	while (i < n_writes) {
		data_buf->write(buf + i * *WINDOW_DATA_BUFS);
		++i;
	}
	if (r)
		data_buf->write(buf + i * *WINDOW_DATA_BUFS, r);
}

off64_t convert_dirent64_to_dirent(char* dirent64_buf, char* dirent_buf, off64_t dirent_64_buf_size) {
	off64_t dirent_buf_size = 0;
	off64_t i = 0;
	struct linux_dirent ld;
	struct linux_dirent64* p_ld64;
	ld.d_reclen = theoretical_size_dirent;
	while (i < dirent_64_buf_size) {
		p_ld64 = (struct linux_dirent64*) (dirent64_buf + i);
		ld.d_ino = p_ld64->d_ino;
		ld.d_off = dirent_buf_size + theoretical_size_dirent;
		logfile << "dirent_buf_size " << dirent_buf_size << std::endl;
		strcpy(ld.d_name, p_ld64->d_name);
		ld.d_name[DNAME_LENGTH + 1] = p_ld64->d_type;
		ld.d_name[DNAME_LENGTH] = '\0'; 
		i += theoretical_size_dirent64;
		memcpy((char*) dirent_buf + dirent_buf_size, &ld, sizeof(ld));
		dirent_buf_size += ld.d_reclen;
	}
	return dirent_buf_size;
}

void handle_pending_read(int tid, int fd, long int process_offset, long int count, bool is_getdents) {
	std::string path = processes_files_metadata[tid][fd];
	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path];
	if (c_file.buf_to_allocate()) {
        #ifdef CAPIOLOG
        logfile << "allocating file " << path << std::endl;
        #endif
		c_file.create_buffer(path, false);
	}
	char* p = c_file.get_buffer();
	sem_post(&files_metadata_sem);
	off64_t end_of_sector = c_file.get_sector_end(process_offset);
	off64_t end_of_read = process_offset + count;
	size_t bytes_read;
	if (end_of_sector > end_of_read) {
		end_of_sector = end_of_read;
		bytes_read = count;
	}
	else
		bytes_read = end_of_sector - process_offset;
	if (is_getdents) {
		sem_wait(&files_metadata_sem);
		off64_t dir_size = c_file.get_stored_size();
		sem_post(&files_metadata_sem);
		off64_t n_entries = dir_size / theoretical_size_dirent64;
		char* p_getdents = (char*) malloc(n_entries * sizeof(char) * dir_size); 
		end_of_sector = convert_dirent64_to_dirent(p, p_getdents, dir_size);
		response_buffers[tid]->write(&end_of_sector);
		send_data_to_client(tid, p_getdents + process_offset, end_of_sector - process_offset);
		free(p_getdents);
	}
	else {
		response_buffers[tid]->write(&end_of_sector);
		send_data_to_client(tid, p + process_offset, bytes_read);
	}
}

void handle_pending_local_reads(const std::string& path, bool wait_for_data) {
	auto it = pending_reads.find(path);
	if (it != pending_reads.end()) {
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
			sem_wait(&files_metadata_sem);
			size_t file_size = files_metadata[path]->get_stored_size();
			sem_post(&files_metadata_sem);
			#ifdef CAPIOLOG
			logfile << "pending read offset " << process_offset << " count " << count << " file_size " << file_size << " wait_for_data " << wait_for_data << std::endl;
			#endif
			if (!wait_for_data || (process_offset + count <= file_size)) {
				#ifdef CAPIOLOG
				logfile << "handling this pending read"<< std::endl;
				#endif
				bool is_getdents = std::get<3>(tuple);
				handle_pending_read(pending_tid, fd, process_offset, count, is_getdents);
				it_vec = pending_reads_this_file.erase(it_vec);
			}
			else
				++it_vec;
		}
		if (!wait_for_data) // then all the pending reads were resolved
			pending_reads.erase(it);
	}
}

/*
 * type == 0 -> regular entry
 * type == 1 -> "." entry
 * type == 2 -> ".." entry
 */

void write_entry_dir(int tid, const std::string& file_path, const std::string& dir, int type) {
	std::hash<std::string> hash;		
	struct linux_dirent64 ld;
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
	long int ld_size = theoretical_size_dirent64;
	ld.d_reclen =  ld_size;
	if (sem_wait(&files_metadata_sem) == -1)
		err_exit("sem_wait files_metadata_sem in write_entry_dir", logfile);
	auto it_tuple = files_metadata.find(dir);

	if (it_tuple == files_metadata.end()) {
		logfile << "dir " << dir << " is not present in CAPIO" << std::endl;
		exit(1);
	}
	
	Capio_file* c_file = it_tuple->second;
	if (c_file->buf_to_allocate()) {
        #ifdef CAPIOLOG
        logfile << "allocating file " << dir << std::endl;
        #endif
		c_file->create_buffer(dir, true);
	}
	void* file_shm = c_file->get_buffer();
	off64_t file_size = c_file->get_stored_size();
	off64_t data_size = file_size + ld_size; //TODO: check theoretical size and sizeof(ld) usage
	off64_t file_shm_size = c_file->get_buf_size();
	ld.d_off =  data_size;
	if (sem_post(&files_metadata_sem) == -1)
		err_exit("sem_post files_metadata_sem in write_entry_dir", logfile);

	if (data_size > file_shm_size) {
        #ifdef CAPIOLOG
        logfile << "handle write data_size > file_shm_size" << std::endl;
        #endif
		file_shm = expand_memory_for_file(data_size, *c_file);
	}
	if (c_file->is_dir()) {
		ld.d_type = DT_DIR; 
	}
	else {
		ld.d_type = DT_REG; 
	}
	ld.d_name[DNAME_LENGTH] = '\0'; 
	memcpy((char*) file_shm + file_size, &ld, sizeof(ld));
	off64_t base_offset = file_size;
	#ifdef CAPIOLOG
	logfile << "insert sector for dir" << base_offset << ", " << data_size << std::endl;
	#endif
	c_file->insert_sector(base_offset, data_size);
	++c_file->n_files;
	std::string committed = c_file->get_committed();
	int pid = pids[tid];
	writers[pid][dir] = true;
	#ifdef CAPIOLOG
	logfile << "nfiles in dir " << dir << " " << c_file->n_files << " " << c_file->n_files_expected << std::endl;
	#endif
	if (c_file->n_files == c_file->n_files_expected) {
		#ifdef CAPIOLOG
		logfile << "dir completed " << std::endl;
		#endif
		c_file->complete = true;
		reply_remote_stats(dir);
	}

	std::string mode = c_file->get_mode();	
	if (mode == "no_update") {
        #ifdef CAPIOLOG
        logfile << "write entry serving remote reads" << std::endl;
        #endif
		handle_pending_remote_reads(dir, data_size, c_file->complete);
		handle_pending_local_reads(dir, false);
	}
		#ifdef CAPIOLOG
		c_file->print(logfile);
		#endif
}

void open_files_metadata(int rank) {
	std::string rank_str = std::to_string(rank);
	std::string file_name = "files_location_" + rank_str + ".txt";
	int fd;
    if ((fd = open(file_name.c_str(), O_WRONLY|O_APPEND|O_CREAT, 0664)) == -1) {
        logfile << "writer error opening file, errno = " << errno << " strerror(errno): " << strerror(errno) << std::endl;
        MPI_Finalize();
        exit(1);
    }
    fd_files_location = fd;
}

void update_dir(int tid, const std::string& file_path, int rank) {
	std::string dir = get_parent_dir_path(file_path);
    #ifdef CAPIOLOG
        logfile << "update dir " << dir << std::endl;
    #endif
	if (sem_wait(&files_metadata_sem) == -1)
		err_exit("sem_wait files_metadata_sem in update_dir", logfile);
	Capio_file* c_file = files_metadata[dir];
    if (c_file->first_write) {
		c_file->first_write = false;
        write_file_location(rank, dir, tid);
	}
	if (sem_post(&files_metadata_sem) == -1)
		err_exit("sem_post files_metadata_sem in update_dir", logfile);
        #ifdef CAPIOLOG
        logfile << "before write entry dir" << std::endl;
        #endif
	write_entry_dir(tid, file_path, dir, 0);
        #ifdef CAPIOLOG
        logfile << "update dir end" << std::endl;
        #endif
}

void update_file_metadata(const std::string& path, int tid, int fd, int rank, bool is_creat) {
	std::string shm_name = path;
	std::replace(shm_name.begin(), shm_name.end(), '/', '_');
	shm_name = shm_name.substr(1);
	//TODO: check the size that the user wrote in the configuration file
	off64_t* p_offset = (off64_t*) create_shm("offset_" + std::to_string(tid) + "_" + std::to_string(fd), sizeof(off64_t));
	sem_wait(&files_metadata_sem);
	auto it = files_metadata.find(path);
	if (it == files_metadata.end()) {
		sem_post(&files_metadata_sem);
		create_file(path, false, file_initial_size);
		it = files_metadata.find(path);
	}
	else
		sem_post(&files_metadata_sem);
	Capio_file* p_capio_file = it->second;
	p_capio_file->add_fd(tid, fd);
	++p_capio_file->n_opens;
	processes_files[tid][fd] = std::make_tuple(p_capio_file, p_offset);//TODO: what happens if a process open the same file twice?
    #ifdef CAPIOLOG
	logfile << "capio open n links " << p_capio_file->n_links << " n opens " << p_capio_file->n_opens << std::endl;;
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

	sem_wait(&files_metadata_sem);
    if (p_capio_file->first_write && is_creat) {
		p_capio_file->first_write = false;
		sem_post(&files_metadata_sem);
        write_file_location(rank, path, tid);
		update_dir(tid, path, rank);
	}
    else
		sem_post(&files_metadata_sem);
}

void handle_crax(const char* str, int rank) {
	int tid, fd;
	char path_cstr[PATH_MAX];
	off64_t res = 1;
	sscanf(str, "crax %d %d %s", &tid, &fd, path_cstr);
	std::string path(path_cstr);
	init_process(tid);
	if (files_metadata.find(path) == files_metadata.end()) {
		res = 0;
		response_buffers[tid]->write(&res);
		update_file_metadata(path, tid, fd, rank, true);
	}
	else
		response_buffers[tid]->write(&res);
}

bool open_static_home_node(std::string path, int tid, int fd, int rank, bool is_creat) {
   auto it = static_home_nodes.find(path);
   bool res = it != static_home_nodes.end() && it->second == node_name;
   if (res)
       update_file_metadata(path, tid, fd, rank, is_creat);
   return res;
}

void handle_open(char* str, int rank, bool is_creat) {
	#ifdef CAPIOLOG
	logfile << "handle open" << std::endl;
	#endif
	int tid, fd;
	char path_cstr[PATH_MAX];
	off64_t res = 0;
	if (is_creat) {
		sscanf(str, "crat %d %d %s", &tid, &fd, path_cstr);
		init_process(tid);
        if (!open_static_home_node(path_cstr, tid, fd, rank, is_creat)) {
            if (files_location.find(path_cstr) != files_location.end() || check_file_location(rank, path_cstr))
                is_creat = false;
            update_file_metadata(path_cstr, tid, fd, rank, is_creat);
        }
        else {
            Capio_file& c_file = *files_metadata[path_cstr];
            c_file.complete = true;
#ifdef CAPIOLOG
            logfile << "file size after open-creat in static home node " << c_file.get_file_size() << std::endl;
#endif
        }
	}
	else {
		sscanf(str, "open %d %d %s", &tid, &fd, path_cstr);
		init_process(tid);
        if (!open_static_home_node(path_cstr, tid, fd, rank, is_creat)) {
        //it is important that check_files_location is the last because is the slowest (short circuit evaluation)
		    if (files_location.find(path_cstr) != files_location.end() || metadata_conf.find(path_cstr) != metadata_conf.end() || match_globs(path_cstr) != -1 || check_file_location(rank, path_cstr)) {
			    update_file_metadata(path_cstr, tid, fd, rank, is_creat);
			    #ifdef CAPIOLOG
			    logfile << "file found" << std::endl;
			    #endif
	    	}
		    else
			    res = 1;
		}
        else {
            Capio_file& c_file = *files_metadata[path_cstr];
            c_file.complete = true;
#ifdef CAPIOLOG
            logfile << "file size after open in static home node " << c_file.get_file_size() << std::endl;
#endif
        }
	}
	response_buffers[tid]->write(&res);
}

void send_file(char* shm, long int nbytes, int dest) {
    if (nbytes == 0) {
#ifdef CAPIOLOG
        logfile << "returning without sending file" << std::endl;
#endif
        return;
    }
    long int num_elements_to_send = 0;
    //MPI_Request req;
    for (long int k = 0; k < nbytes; k += num_elements_to_send) {
        if (nbytes - k > 1024L * 1024 * 1024)
            num_elements_to_send = 1024L * 1024 * 1024;
        else
            num_elements_to_send = nbytes - k;
#ifdef CAPIOLOG
        logfile << "before sending file k " << k << " num elem to send " << num_elements_to_send << std::endl;
#endif
        //MPI_Send(shm + k, num_elements_to_send, MPI_BYTE, dest, 0, MPI_COMM_WORLD);
        MPI_Isend(shm + k, num_elements_to_send, MPI_BYTE, dest, 0, MPI_COMM_WORLD, &req);
#ifdef CAPIOLOG
        logfile << "after sending file" << std::endl;
#endif
    }
}

//TODO: refactor offset_str and offset

void serve_remote_read(const char* path_c, int dest, long int offset, long int nbytes, int complete) {
    if (sem_wait(&remote_read_sem) == -1)
        err_exit("sem_wait remote_read_sem in serve_remote_read", logfile);

    char* buf_send;
    // Send all the rest of the file not only the number of bytes requested
    // Useful for caching
    void* file_shm;
    size_t file_size;
    sem_wait(&files_metadata_sem);
    if (files_metadata.find(path_c) != files_metadata.end()) {
        Capio_file& c_file = *files_metadata[path_c];
        file_shm = c_file.get_buffer();
        file_size = c_file.get_stored_size();
        sem_post(&files_metadata_sem);
    }
    else {
        sem_post(&files_metadata_sem);
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
#ifdef CAPIOLOG
    logfile << "before sending part of the file to : " << dest << " with offset " << offset << " nbytes" << nbytes << std::endl;
#endif
    send_file(((char*) file_shm) + offset, nbytes, dest);
#ifdef CAPIOLOG
    logfile << "after sending part of the file to : " << dest << std::endl;
#endif
    if (sem_post(&remote_read_sem) == -1) {
        err_exit("sem_post remote_read_sem in serve_remote_read", logfile);
    }
}

void handle_write(const char* str, int rank) {
        //check if another process is waiting for this data
        std::string request;
        int tid, fd;
		off_t base_offset;
        off64_t count, data_size;
        std::istringstream stream(str);
        stream >> request >> tid >> fd >> base_offset >> count;
		data_size = base_offset + count;
        std::string path = processes_files_metadata[tid][fd];
		sem_wait(&files_metadata_sem);

		Capio_file& c_file = *files_metadata[path];
		sem_post(&files_metadata_sem);
		if (c_file.buf_to_allocate()) {
        #ifdef CAPIOLOG
        logfile << "allocating file " << path << std::endl;
        #endif
			c_file.create_buffer(path, true);
		}
		off64_t file_shm_size = c_file.get_buf_size();
		auto* data_buf = data_buffers[tid].first;
		size_t n_reads = count / *WINDOW_DATA_BUFS;  
		size_t r = count % *WINDOW_DATA_BUFS;
		size_t i = 0;
		char* p;
		if (data_size > file_shm_size) {

        #ifdef CAPIOLOG
        logfile << "handle write data_size > file_shm_size " << data_size << " " << file_shm_size << std::endl;
        #endif
			p = expand_memory_for_file(data_size, c_file);
		}
		p = c_file.get_buffer();
		p = p + base_offset;
        #ifdef CAPIOLOG
		logfile << "debug handle_write 0 " << std::endl;
        #endif
		while (i < n_reads) {
			data_buf->read(p + i * *WINDOW_DATA_BUFS);
			++i;
		}
        #ifdef CAPIOLOG
		logfile << "debug handle_write 3 " << std::endl;
        #endif
		if (r)
			data_buf->read(p + i * *WINDOW_DATA_BUFS, r);

        #ifdef CAPIOLOG
		logfile << "debug handle_write 4 " << std::endl;
        #endif

		int pid = pids[tid];
		writers[pid][path] = true;
        #ifdef CAPIOLOG
		logfile << "insert sector " << base_offset << ", " << data_size << std::endl;
        #endif
		c_file.insert_sector(base_offset, data_size);
        #ifdef CAPIOLOG
		c_file.print(logfile);
        logfile << "handle write tid fd " << tid << " " << fd << std::endl;
		logfile << "path " << path << std::endl;
        #endif
		sem_wait(&files_metadata_sem);
        if (c_file.first_write) {
			c_file.first_write = false;
            write_file_location(rank, path, tid);
			sem_post(&files_metadata_sem);
			//TODO: it works only if there is one prod per file
			update_dir(tid, path, rank);
        }
		else
			sem_post(&files_metadata_sem);
		sem_wait(&files_metadata_sem);
		std::string mode = c_file.get_mode();
		sem_post(&files_metadata_sem);
        #ifdef CAPIOLOG
        logfile << "mode is " << mode << std::endl;
        #endif

	if (mode == "no_update") {
        #ifdef CAPIOLOG
        logfile << "handle write serving remote pending reads" << std::endl;
        #endif
		handle_pending_local_reads(path, true);
		handle_pending_remote_reads(path, data_size, false);
	}

    auto it = static_home_nodes.find(path);
    if (it != static_home_nodes.end()) {
        std::string node = it->second;
        if (node != node_name) {
            int node_rank = nodes_helper_rank[node];
#ifdef CAPIOLOG
            logfile << "sending write to remote node " << node << std::endl;
#endif
            serve_remote_read(path.c_str(), node_rank, base_offset, count, false);
        }
    }
	

}



/*
 * Multithread function
 */

void handle_local_read(int tid, int fd, off64_t count, bool dir, bool is_getdents, bool is_prod) {
		#ifdef CAPIOLOG
		logfile << "handle local read" << std::endl;
		#endif
		if (sem_wait(&handle_local_read_sem) == -1)
			err_exit("sem_wait handle_local_read_sem in handle_local_read", logfile);
		std::string path = processes_files_metadata[tid][fd];
		sem_wait(&files_metadata_sem);
		Capio_file & c_file = *files_metadata[path];
		sem_post(&files_metadata_sem);
		off64_t process_offset = *std::get<1>(processes_files[tid][fd]);
		int pid = pids[tid];
		bool writer = writers[pid][path];
		off64_t end_of_sector = c_file.get_sector_end(process_offset);
		#ifdef CAPIOLOG
		logfile << "Am I a writer? " << writer << std::endl;
		logfile << "process offset " << process_offset << std::endl;
		logfile << "count " << count << std::endl;
		logfile << "end of sector" << end_of_sector << std::endl;
		c_file.print(logfile);
        #endif
		off64_t end_of_read = process_offset + count;
		std::string committed = c_file.get_committed();
		std::string mode = c_file.get_mode();
		if (mode != "no_update" && !c_file.complete && !writer && !is_prod) {
			#ifdef CAPIOLOG
			logfile << "add pending reads 1" << std::endl;
			logfile << "mode " << mode << std::endl;
			logfile << "file complete " << c_file.complete << std::endl;
			#endif
			pending_reads[path].emplace_back(tid, fd, count, is_getdents);
		}
		else if (end_of_read > end_of_sector ) {
		#ifdef CAPIOLOG
		logfile << "Is the file completed? " << c_file.complete << std::endl;
		#endif
			if (!is_prod && !writer && !c_file.complete && (!dir || end_of_sector - process_offset <= 0)) {
				#ifdef CAPIOLOG
				logfile << "add pending reads 2" << std::endl;
				#endif
				pending_reads[path].emplace_back(tid, fd, count, is_getdents);
			}
			else {
			  if (end_of_sector == -1) {
			    end_of_sector = 0;
			    response_buffers[tid]->write(&end_of_sector);
			    if (sem_post(&handle_local_read_sem) == -1)
			      err_exit("sem_post handle_local_read_sem in handle_local_read", logfile);
			    return;
			  }
				sem_wait(&files_metadata_sem);
				if (c_file.buf_to_allocate()) {

        #ifdef CAPIOLOG
        logfile << "allocating file " << path << std::endl;
        #endif
					c_file.create_buffer(path, false);
				}
				char* p = c_file.get_buffer();
				if (is_getdents) {
					off64_t dir_size = c_file.get_stored_size();
					sem_post(&files_metadata_sem);
					off64_t n_entries = dir_size / theoretical_size_dirent64;
					char* p_getdents = (char*) malloc(n_entries * sizeof(char) * dir_size); 
					end_of_sector = convert_dirent64_to_dirent(p, p_getdents, dir_size);
					response_buffers[tid]->write(&end_of_sector);
					send_data_to_client(tid, p_getdents + process_offset, end_of_sector - process_offset);
					free(p_getdents);
				}
				else {
					sem_post(&files_metadata_sem);
		#ifdef CAPIOLOG
		logfile << "debug bbb end_of_sector " << end_of_sector << " count " << count << " process_offset " << process_offset << std::endl;
		#endif
					response_buffers[tid]->write(&end_of_sector);
					send_data_to_client(tid, p + process_offset, end_of_sector - process_offset);
				}
			}
		}
		else {
			sem_wait(&files_metadata_sem);
			if (c_file.buf_to_allocate()) {
        #ifdef CAPIOLOG
        logfile << "allocating file " << path << std::endl;
        #endif
				c_file.create_buffer(path, false);
				}
			char* p = c_file.get_buffer();
			size_t bytes_read;
			bytes_read = count;
		#ifdef CAPIOLOG
		logfile << "debug aaa end_of_sector " << end_of_sector << " bytes_read " << bytes_read << " count " << count << " process_offset " << process_offset << std::endl;
		#endif
			if (is_getdents) {
				off64_t dir_size = c_file.get_stored_size();
				sem_post(&files_metadata_sem);
				off64_t n_entries = dir_size / theoretical_size_dirent64;
				char* p_getdents = (char*) malloc(n_entries * sizeof(char) * dir_size); 
				end_of_sector = convert_dirent64_to_dirent(p, p_getdents, dir_size);
				response_buffers[tid]->write(&end_of_read);
				send_data_to_client(tid, p_getdents + process_offset, bytes_read);
				free(p_getdents);
			}
			else {
				sem_post(&files_metadata_sem);
#ifdef CAPIOLOG
                logfile << "read responding " << end_of_read << std::endl;
#endif
				response_buffers[tid]->write(&end_of_read);
#ifdef CAPIOLOG
                logfile << "read giving data " << bytes_read << std::endl;
#endif
				send_data_to_client(tid, p + process_offset, bytes_read);
			}
		}
		#ifdef CAPIOLOG
		logfile << "process offset " << process_offset << std::endl;
		#endif
		if (sem_post(&handle_local_read_sem) == -1)
			err_exit("sem_post handle_local_read_sem in handle_local_read", logfile);
	    
}


bool read_from_local_mem(int tid, off64_t process_offset, off64_t end_of_read,
		off64_t end_of_sector, off64_t count, const std::string& path) {
	#ifdef CAPIOLOG
	logfile << "reading from local memory" << std::endl;
	#endif
	bool res = false;
	if (end_of_read <= end_of_sector) {
		Capio_file& c_file = *files_metadata[path];
		sem_wait(&files_metadata_sem);
		if (c_file.buf_to_allocate()) {
        #ifdef CAPIOLOG
        logfile << "allocating file " << path << std::endl;
        #endif
			c_file.create_buffer(path, false);
			}
		char* p = c_file.get_buffer();
		sem_post(&files_metadata_sem);
		response_buffers[tid]->write(&end_of_read);
		send_data_to_client(tid, p + process_offset, count);
		res = true;
	}
	return res;
}

/*
 * Multithread function
 */

void handle_remote_read(int tid, int fd, off64_t count, int rank, bool dir, bool is_getdents) {
		#ifdef CAPIOLOG
		logfile << "handle remote read before sem_wait" << std::endl;
		#endif
		//before sending the request to the remote node, it checks
		//in the local cache

		if (sem_wait(&handle_remote_read_sem) == -1)
			err_exit("sem_wait handle_remote_read_sem in handle_remote_read", logfile);

		std::string path = processes_files_metadata[tid][fd];
		sem_wait(&files_metadata_sem);
		Capio_file& c_file = *files_metadata[path];
		size_t real_file_size = c_file.real_file_size;
		sem_post(&files_metadata_sem);
		off64_t process_offset = *std::get<1>(processes_files[tid][fd]);
		off64_t end_of_read = process_offset + count;
		off64_t end_of_sector = c_file.get_sector_end(process_offset);
		#ifdef CAPIOLOG
		logfile << "complete " << c_file.complete << " end_of_read " << end_of_read << std::endl;
		logfile << " end_of_sector " << end_of_sector << " real_file_size " << real_file_size << std::endl;
		#endif
		std::size_t eos;
		if (end_of_sector == -1)
			eos = 0;
		else 
			eos = end_of_sector;
		if (c_file.complete && (end_of_read <= end_of_sector || eos == real_file_size)) {
			handle_local_read(tid, fd, count, dir, is_getdents, true);
			if (sem_post(&handle_remote_read_sem) == -1)
				err_exit("sem_post handle_remote_read_sem in handle_remote_read", logfile);

			return;
		}
		bool res = read_from_local_mem(tid, process_offset, end_of_read, end_of_sector, count, path); //when is not complete but mode = no_update
		if (res) { // it means end_of_read < end_of_sector
			if (sem_post(&handle_remote_read_sem) == -1)
				err_exit("sem_post handle_remote_read_sem in handle_remote_read", logfile);
			return;
		}
		#ifdef CAPIOLOG
		logfile << "handle remote read after sem_wait" << std::endl;
		#endif
		// If it is not in cache then send the request to the remote node
		const char* msg;
		std::string str_msg;
		int dest = nodes_helper_rank[std::get<0>(files_location[processes_files_metadata[tid][fd]])];
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
		my_remote_pending_reads[processes_files_metadata[tid][fd]].emplace_back(tid, fd, count, is_getdents);
		if (sem_post(&handle_remote_read_sem) == -1)
			err_exit("sem_post handle_remote_read_sem in handle_remote_read", logfile);
}

struct wait_for_file_metadata{
	int tid;
	int fd;
	bool dir;
	bool is_getdents;
	off64_t count;
};

void loop_check_files_location(const std::string& path_to_check, int rank) {
	
	#ifdef CAPIOLOG
	logfile << "wait for file before" << std::endl;
	#endif
	struct timespec sleepTime, returnTime;
    sleepTime.tv_sec = 0;
    sleepTime.tv_nsec = 200000;
	bool found = false;
	while (!found) {
		nanosleep(&sleepTime, &returnTime);
		found = check_file_location(rank, path_to_check);
	}

	#ifdef CAPIOLOG
	logfile << "wait for file after" << std::endl;
	#endif
}

bool handle_nreads(const std::string& path, const std::string& app_name, int dest) {
	bool success = false;

	long int pos = match_globs(path);
	if (pos != -1) {
		#ifdef CAPIOLOG
			logfile << "glob matched" << std::endl;
		#endif
		std::string glob = std::get<0>(metadata_conf_globs[pos]);
		std::size_t batch_size = std::get<5>(metadata_conf_globs[pos]);
		if (batch_size > 0) {
			char* msg = (char*) malloc(sizeof(char) * (512 + PATH_MAX));
			sprintf(msg, "nrea %zu %s %s %s", batch_size, app_name.c_str(), glob.c_str(), path.c_str());
			MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
			success = true;
			#ifdef CAPIOLOG
			logfile << "handling nreads, msg: " << msg << " msg size " << strlen(msg) + 1 << std::endl;
			#endif
			free(msg);
			return success;
		}
	}
	return success;
}

void* wait_for_file(void* pthread_arg) {
	auto* metadata = (struct wait_for_file_metadata*) pthread_arg;
	int tid = metadata->tid;
	int fd = metadata-> fd;
	off64_t count = metadata->count;
	bool dir = metadata->dir;
	bool is_getdents = metadata->is_getdents;
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	std::string path_to_check(processes_files_metadata[tid][fd]);
	loop_check_files_location(path_to_check, rank);

	//check if the file is local or remote
	if (strcmp(std::get<0>(files_location[path_to_check]), node_name) == 0) {
			handle_local_read(tid, fd, count, dir, is_getdents, false);
	}
	else {
		sem_wait(&files_metadata_sem);
		Capio_file& c_file = *files_metadata[path_to_check];
		sem_post(&files_metadata_sem);
		if (!c_file.complete) {
			auto it = apps.find(tid);
			bool res = false;
			if (it != apps.end()) {
				std::string app_name = it->second;
				res = handle_nreads(path_to_check, app_name, nodes_helper_rank[std::get<0>(files_location[path_to_check])]);
			}
			if (res) {
				if (sem_wait(&handle_remote_read_sem) == -1)
					err_exit("sem_wait handle_remote_read_sem in wait_for_file", logfile);
				my_remote_pending_reads[path_to_check].emplace_back(tid, fd, count, is_getdents);
				if (sem_post(&handle_remote_read_sem) == -1)
					err_exit("sem_post handle_remote_read_sem in wait_for_file", logfile);
				return nullptr;
			}
		}
			#ifdef CAPIOLOG
			logfile << "handle remote read in wait for file" << std::endl;	
			logfile << "path to check " << path_to_check << std::endl;
			#endif
			handle_remote_read(tid, fd, count, rank, dir, is_getdents);
	}

	free(metadata);
	return nullptr;
}

std::string get_producer_name(const std::string& path) {
	std::string producer_name;
	//we handle also prefixes
	auto it_metadata = metadata_conf.find(path);
	if (it_metadata == metadata_conf.end()) {
		long int pos = match_globs(path);
		if (pos != -1) {
			producer_name = std::get<3>(metadata_conf_globs[pos]);
	#ifdef CAPIOLOG
	logfile << "pos " << pos << " producer_name " << producer_name << std::endl;
	#endif
		}
	}
	else {
		producer_name = std::get<2>(it_metadata->second);
	#ifdef CAPIOLOG
	logfile << "get producer_name " << producer_name << std::endl;
	#endif
	}
	return producer_name;
}

bool is_producer(int tid, const std::string& path) {
	bool res = false;
	auto it = apps.find(tid);
	#ifdef CAPIOLOG
	logfile << "is producer " << path << std::endl;
	#endif
	if (it != apps.end()) {
		std::string app_name = apps[tid];
		std::string prod_name = get_producer_name(path);
		#ifdef CAPIOLOG
		logfile << "app_name " << app_name << " prod_name " << prod_name << std::endl;
		#endif
		res = app_name == prod_name;
	}
	return res;
}



void handle_read(const char* str, int rank, bool dir, bool is_getdents) {
	#ifdef CAPIOLOG
	logfile << "handle read str" << str << std::endl;
	#endif
	std::string request;
	int tid, fd;
	off64_t count;
	std::istringstream stream(str);
	stream >> request >> tid >> fd >> count;
	std::string path = processes_files_metadata[tid][fd];
	bool is_prod = is_producer(tid, path);

    auto it = static_home_nodes.find(path);
    if (it != static_home_nodes.end()) {
        std::string node = it->second;
        if (node != node_name) {
            logfile << "error static home node read" << std::endl;
            exit(EXIT_FAILURE);
        }
        handle_local_read(tid, fd, count, dir, is_getdents, is_prod);
        return;
    }

	if (files_location.find(path) == files_location.end() && !is_prod) {
		bool found = check_file_location(rank, processes_files_metadata[tid][fd]);
		if (!found) {
			//launch a thread that checks when the file is created
			pthread_t t;
			auto* metadata = (struct wait_for_file_metadata*)  malloc(sizeof(wait_for_file_metadata));
			metadata->tid = tid;
			metadata->fd = fd;
			metadata->count = count;
			metadata->dir = dir;
			metadata->is_getdents = is_getdents;
			int res = pthread_create(&t, nullptr, wait_for_file, (void*) metadata);
			if (res != 0) {
				logfile << "error creation of capio server thread (handle read wait for file)" << std::endl;
				MPI_Finalize();
				exit(1);
			}

			return;
		}
	}

	if (is_prod || strcmp(std::get<0>(files_location[path]), node_name) == 0 || *capio_dir == path) {
		handle_local_read(tid, fd, count, dir, is_getdents, is_prod);
	}
	else {
		sem_wait(&files_metadata_sem);
		Capio_file& c_file = *files_metadata[path];
		sem_post(&files_metadata_sem);
		if (!c_file.complete) {
			auto it = apps.find(tid);
			bool res = false;
			if (it != apps.end()) {
				std::string app_name = it->second;
				if (!dir)
					res = handle_nreads(path, app_name, nodes_helper_rank[std::get<0>(files_location[path])]);
			}
			if (res) {
				if (sem_wait(&handle_remote_read_sem) == -1)
					err_exit("sem_wait handle_remote_read_sem in handle_read", logfile);
				my_remote_pending_reads[path].push_back(std::make_tuple(tid, fd, count, is_getdents));
				if (sem_post(&handle_remote_read_sem) == -1)
					err_exit("sem_post handle_remote_read_sem in handle_read", logfile);
				return;
			}
		}
	#ifdef CAPIOLOG
	logfile << "before handle remote read handle read" << std::endl;
	#endif	
	
		handle_remote_read(tid, fd, count, rank, dir, is_getdents);
	}
}

/*
 * Returns 0 if the file "file_name" does not exist
 * Returns 1 if the location of the file path_to_check is found
 * Returns 2 otherwise
 *
*/

int delete_from_file_locations(const std::string& file_name, std::string path_to_remove, int rank) {
    FILE * fp;
    char* line = nullptr;
    size_t len = 0;
    ssize_t read = 0;
    int res = 0;
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;    /* shared lock for read*/
    lock.l_whence = SEEK_SET; /* base for seek offsets */
    lock.l_start = 0;         /* 1st byte in file */
    lock.l_len = 0;           /* 0 here means 'until EOF' */
    lock.l_pid = getpid();    /* process id */

    int fd; /* file descriptor to identify a file within a process */
    //fd = open(file_name.c_str(), O_RDONLY);  /* -1 signals an error */
    fp = fopen(file_name.c_str(), "r+");
	if (!fp) {
		logfile << "capio server " << rank << " failed to open the location file" << std::endl;
		return 0;
	}
	fd = fileno(fp);
	if (fd == -1)
		err_exit("fileno delete_from_file_location", logfile);
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        close(fd);
		err_exit("fcntl lock in delete_from_file_location", logfile);
    }
	const char* path_to_check_cstr = path_to_remove.c_str();
	bool found = false;
	long byte = 0;
    while (read != -1 && !found) {
		byte = ftell(fp);
		if (byte == -1)
			err_exit("ftell delete_from_file_location", logfile);
			
		read = getline(&line, &len, fp);
		if (read == -1)
			break;
		if (line[0] == '0')
			continue;
		char path[1024]; //TODO: heap memory
		int i = 0;
		while(line[i] != ' ') {
			path[i] = line[i];
			++i;
		}
		path[i] = '\0';
		if (strcmp(path, path_to_check_cstr) == 0) {
			found = true;
		}
		//check if the file is present
    }
	if (found) {
		char del_char = '0';
		if (fseek(fp, byte, SEEK_SET) == -1)
			err_exit("fseek delete_from_file_location", logfile);
		fwrite(&del_char, sizeof(char), 1, fp); 
		res = 1;	
    	#ifdef CAPIOLOG
    		logfile << "deleting line" << path_to_remove << std::endl;
    	#endif
	}
	else
		res = 2;
    /* Release the lock explicitly. */
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
		err_exit("fctl unlock in delete_from_file_location", logfile);
    }
    if (fclose(fp) == EOF)
		err_exit("fclose delete_from_file_location", logfile);
    return res;
}

void delete_from_file_locations(const std::string& path_metadata, long int offset, int my_rank, std::size_t rank) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;    /* shared lock for read*/
    lock.l_whence = SEEK_SET; /* base for seek offsets */
    lock.l_start = 0;         /* 1st byte in file */
    lock.l_len = 0;           /* 0 here means 'until EOF' */
    lock.l_pid = getpid();    /* process id */
	int fd;
    //fd = open(file_name.c_str(), O_RDONLY);  /* -1 signals an error */
	if (rank < fd_files_location_reads.size()) {

	#ifdef CAPIOLOG
	logfile << "fast remove if " << offset << std::endl;
	#endif	
    	fd = std::get<0>(fd_files_location_reads[rank]); 
	}
	else {

	#ifdef CAPIOLOG
	logfile << "fast remove else " << offset << std::endl;
	#endif	
		std::string index_str = std::to_string(rank);
		std::string file_name = "files_location_" + std::to_string(rank) + ".txt";
    	FILE* fp = fopen(file_name.c_str(), "r+");
		if (!fp)
			err_exit("fopen " + file_name + " delete_from_file_locations", logfile);
		fd = fileno(fp);
		if (fd == -1)
			err_exit("fileno delete_from_file_locations", logfile);
		fd_files_location_reads.emplace_back(fd, fp, false);
	}
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
		err_exit("fcntl delete_from_file_locations", logfile);
    }
	#ifdef CAPIOLOG
	logfile << "fast remove offset " << offset << std::endl;
	#endif	
	char del_char = '0';
	long old_offset = lseek(fd, 0, SEEK_CUR);
	if (old_offset == -1)
		err_exit("lseek 1 delete_from_file_locations", logfile);
	if (lseek(fd, offset, SEEK_SET) == -1)
		err_exit("lseek 2 delete_from_file_locations", logfile);
	write(fd, &del_char, sizeof(char)); 
	if (lseek(fd, old_offset, SEEK_SET) == -1)
		err_exit("lseek 3 delete_from_file_locations", logfile);
    /* Release the lock explicitly. */
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        logfile << "reader " << my_rank << " failed to unlock the file" << std::endl;
    }

}

void delete_from_metadata(const std::string& path_to_remove, int my_rank) {
	bool found = false;
	int rank = 0, res = -1;
	std::string node = std::get<0>(files_location[path_to_remove]);
	long int offset = std::get<1>(files_location[path_to_remove]);
	if (offset == -1) { //TODO: very inefficient
	#ifdef CAPIOLOG
	logfile << "very slow remove" << std::endl;
	#endif	
		while (!found && rank < n_servers) {
			std::string rank_str = std::to_string(rank);
			res = delete_from_file_locations("files_location_" + rank_str + ".txt", path_to_remove, my_rank);
			found = res == 1;
			++rank;
		}
	}
	else {
        #ifdef CAPIOLOG
	        logfile << "fast remove" << std::endl;
	    #endif
        std::string file_node_name = std::get<0>(files_location[path_to_remove]);
	    #ifdef CAPIOLOG
	    logfile << "fast remove node_name " << file_node_name << std::endl;
	    #endif
		if (file_node_name == std::string(node_name))	
			rank = my_rank;
		else
			rank = nodes_helper_rank[file_node_name];
	    #ifdef CAPIOLOG
	    logfile << "fast remove rank " << rank << std::endl;
	    #endif
		delete_from_file_locations("files_location_" + std::to_string(rank) + ".txt", offset, my_rank, rank);
	}
}

void delete_file(const std::string& path, int rank) {
	#ifdef CAPIOLOG
	logfile << "deleting file " << path << std::endl;
	#endif	
	sem_wait(&files_metadata_sem);
	files_metadata.erase(path);
	sem_post(&files_metadata_sem);
	delete_from_metadata(path, rank);
	files_location.erase(path);
   	for (auto& pair : writers) {
		auto& files = pair.second;	
		files.erase(path);
	}
}

void handle_pending_remote_nfiles(const std::string& path) {
	#ifdef CAPIOLOG
	logfile << "handle pending remote nfiles" << std::endl;;
	#endif
	if (sem_wait(&clients_remote_pending_nfiles_sem) == -1)
		err_exit("sem_wait clients_remote_pending_nfiles_sem in handle_pending_remote_nfiles", logfile);
	for (auto& p : clients_remote_pending_nfiles) {
		std::string app = p.first;
		std::list<struct remote_n_files*>& app_pending_nfiles = p.second;
		auto it = app_pending_nfiles.begin();
		std::list<struct remote_n_files*>::iterator next_it;
		while (it != app_pending_nfiles.end()) {
			std::string prefix = (*it)->prefix;
			std::unordered_set<std::string> &files = files_sent[app];
			if (sem_wait(&files_location_sem) == -1)
				err_exit("sem_wait files_location_sem in handle_pending_remote_nfiles", logfile);
			auto it_fs = files_location.find(path);
			next_it =  std::next(it);
			if (files.find(path) == files.end() && it_fs != files_location.end() && strcmp(std::get<0>(it_fs->second), node_name) == 0 && path.compare(0, prefix.length(), prefix) == 0) {
				if (sem_post(&files_location_sem) == -1)
					err_exit("sem_post files_location_sem in handle_pending_remote_nfiles", logfile);
				(*it)->files_path->push_back(path);
				files.insert(path);
				if ((*it)->files_path->size() == (*it)->n_files) {
					#ifdef CAPIOLOG
					logfile << "waking up thread " << std::endl;;
					#endif
					sem_t* sem = (*it)->sem;
					app_pending_nfiles.erase(it);
					if (sem_post(sem) == -1)
						err_exit("sem_post sem in handle_pending_remote_nfiles", logfile);
				}
			}
			else {
				if (sem_post(&files_location_sem) == -1)
					err_exit("sem_post files_location_sem in handle_pending_remote_nfiles", logfile);
			}

			it = next_it;
		}
	}
	if (sem_post(&clients_remote_pending_nfiles_sem) == -1)
		err_exit("sem_post clients_remote_pending_nfiles_sem in handle_pending_remote_nfiles", logfile);
}

void handle_close(int tid, int fd, int rank) {
	std::string path = processes_files_metadata[tid][fd];
	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path];
	sem_post(&files_metadata_sem);
	++c_file._n_close;
	if (c_file.get_committed() == "on_close" && (c_file._n_close_expected == -1 || c_file._n_close == c_file._n_close_expected)) {
		#ifdef CAPIOLOG
		logfile <<  "handle close, committed = on_close. n_close " << c_file._n_close << " n_close_expected " << c_file._n_close_expected << std::endl;
		#endif
		c_file.complete = true;
		handle_pending_local_reads(path, false);	
		reply_remote_stats(path);	
		//TODO: error if seek are done and also do this on exit
		handle_pending_remote_reads(path, c_file.get_sector_end(0), true);
		handle_pending_remote_nfiles(path);
		c_file.commit();
	}

	--c_file.n_opens;
	#ifdef CAPIOLOG
	logfile << "capio close n links " << c_file.n_links << " n opens " << c_file.n_opens << std::endl;;
	#endif
	if (c_file.n_opens == 0 && c_file.n_links <= 0)
		delete_file(path, rank);
	std::string offset_name = "offset_" + std::to_string(tid) +  "_" + std::to_string(fd);
	shm_unlink(offset_name.c_str()); // no check because could be a dup fd
	/*if (shm_unlink(offset_name.c_str()) == -1)
		err_exit("shm_unlink " + offset_name + " in handle_close");*/
	processes_files[tid].erase(fd);
	processes_files_metadata[tid].erase(fd);
	c_file.remove_fd(tid, fd);
}

void handle_close(char* str, char* p, int rank) {
	int tid, fd;
	sscanf(str, "clos %d %d", &tid, &fd);
	#ifdef CAPIOLOG
	logfile << "handle close " << tid << " " << fd << std::endl;
	#endif
	handle_close(tid, fd, rank);
}

/*
 *	Multi-threaded function
 */

void solve_remote_reads(size_t bytes_received, size_t offset, size_t file_size, const char* path_c, bool complete) {
    #ifdef CAPIOLOG
	logfile << " solve remote reads before semwait " << std::endl;
	#endif
	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path_c];
	sem_post(&files_metadata_sem);
	c_file.real_file_size = file_size;
    #ifdef CAPIOLOG
	logfile << "insert offset " << offset << " bytes_received " << bytes_received << std::endl;
#endif
	c_file.insert_sector(offset, offset + bytes_received);
	c_file.complete = complete;
	std::string path(path_c);
	int tid, fd;
	long int count; //TODO: diff between count and bytes_received
	if (sem_wait(&handle_remote_read_sem) == -1)
		err_exit("sem_wait handle_remote_read_sem in solve_remote_reads", logfile);
	std::list<std::tuple<int, int, long int, bool>>& list_remote_reads = my_remote_pending_reads[path];
	auto it = list_remote_reads.begin();
	std::list<std::tuple<int, int, long int, bool>>::iterator prev_it;
	off64_t end_of_sector;
	while (it != list_remote_reads.end()) {
		tid = std::get<0>(*it);
		fd = std::get<1>(*it);
		count = std::get<2>(*it);
		bool is_getdent = std::get<3>(*it);
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
			if (c_file.buf_to_allocate()) {
        #ifdef CAPIOLOG
        logfile << "allocating file " << path << std::endl;
        #endif
				c_file.create_buffer(path, false);
				}
			char* p = c_file.get_buffer();

			size_t bytes_read;
			off64_t end_of_read = fd_offset + count;
			if (end_of_sector > end_of_read) {
				end_of_sector = end_of_read;
				bytes_read = count;
			}
			else
				bytes_read = end_of_sector - fd_offset;

			if (is_getdent) {
				sem_wait(&files_metadata_sem);
				off64_t dir_size = c_file.get_stored_size();
				sem_post(&files_metadata_sem);
				off64_t n_entries = dir_size / theoretical_size_dirent64;
				char* p_getdents = (char*) malloc(n_entries * sizeof(char) * dir_size); 
				end_of_sector = convert_dirent64_to_dirent(p, p_getdents, dir_size);
				response_buffers[tid]->write(&end_of_sector);
				send_data_to_client(tid, p_getdents + fd_offset, bytes_read);
				free(p_getdents);
			}
			else {	
				response_buffers[tid]->write(&end_of_sector);
				send_data_to_client(tid, p + fd_offset, bytes_read);
			}

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
	if (sem_post(&handle_remote_read_sem) == -1)
		err_exit("sem_post handle_remote_read_sem in solve_remote_reads", logfile);
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
	solve_remote_reads(bytes_received, offset, file_size, path_c, complete);
}

void handle_lseek(char* str) {
	int tid, fd;	
	size_t offset;
	sscanf(str, "seek %d %d %zu", &tid, &fd, &offset);
	std::string path = processes_files_metadata[tid][fd];
	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path];
	sem_post(&files_metadata_sem);
	off64_t sector_end = c_file.get_sector_end(offset);
	response_buffers[tid]->write(&sector_end);
}

void handle_sdat(char* str) {
	int tid, fd;	
	size_t offset;
	sscanf(str, "sdat %d %d %zu", &tid, &fd, &offset);
	std::string path = processes_files_metadata[tid][fd];
	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path];
	sem_post(&files_metadata_sem);
	off64_t res = c_file.seek_data(offset);
	response_buffers[tid]->write(&res);
}

void handle_shol(char* str) {
	int tid, fd;	
	size_t offset;
	sscanf(str, "shol %d %d %zu", &tid, &fd, &offset);
	std::string path = processes_files_metadata[tid][fd];
	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path];
	sem_post(&files_metadata_sem);
	off64_t res = c_file.seek_hole(offset);
	response_buffers[tid]->write(&res);
}


void handle_local_stat(int tid, std::string path) {
	off64_t file_size;
	sem_wait(&files_metadata_sem);
	auto it = files_metadata.find(path);
	Capio_file& c_file = *(it->second);
	sem_post(&files_metadata_sem);
	file_size = c_file.get_file_size();
	off64_t is_dir;
	if (c_file.is_dir())
		is_dir = 0;
	else
		is_dir = 1;
	response_buffers[tid]->write(&file_size);
	response_buffers[tid]->write(&is_dir);
	#ifdef CAPIOLOG
		logfile << "file size handle local stat : " << file_size << std::endl;
	#endif
}

void handle_remote_stat(int tid, const std::string& path, int rank) {
	#ifdef CAPIOLOG
	logfile << "handle remote stat before sem_wait" << std::endl;
	#endif
	if (sem_wait(&handle_remote_stat_sem) == -1)
		err_exit("sem_wait handle_remote_stat_sem in handle_remote_stat", logfile);
	std::string str_msg;
	int dest = nodes_helper_rank[std::get<0>(files_location[path])];
	str_msg = "stat " + std::to_string(rank) + " " + path; 
	const char* msg = str_msg.c_str();
	#ifdef CAPIOLOG
	logfile << "handle remote stat" << std::endl;
	logfile << "msg sent " << msg << std::endl;
	logfile << "dest " << dest << std::endl;
	logfile << "rank" << rank << std::endl;
	#endif
	MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
	#ifdef CAPIOLOG
	logfile << "remote stat 0" << std::endl;
	#endif
	my_remote_pending_stats[path].push_back(tid);
	#ifdef CAPIOLOG
	logfile << "remote stat 1" << std::endl;
	#endif
	if (sem_post(&handle_remote_stat_sem) == -1)
		err_exit("sem_post handle_remote_stat_sem in handle_remote_stat", logfile);
	#ifdef CAPIOLOG
	logfile << "remote stat 2" << std::endl;
	#endif

}


struct wait_for_stat_metadata{
	int tid;
	char path[PATH_MAX];
};

void* wait_for_stat(void* pthread_arg) {
	auto* metadata = (struct wait_for_stat_metadata*) pthread_arg;
	int tid = metadata->tid;
	const char* path = metadata->path;
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	std::string path_to_check(path);
        #ifdef CAPIOLOG
        logfile << "wait for stat" << std::endl;
        #endif
	loop_check_files_location(path_to_check, rank);

	//check if the file is local or remote

	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path];
	sem_post(&files_metadata_sem);
	std::string mode = c_file.get_mode();
	bool complete = c_file.complete;
	if (complete || strcmp(std::get<0>(files_location[path_to_check]), node_name) == 0 || mode == "no_update") {
		handle_local_stat(tid, path);
	}
	else {
		handle_remote_stat(tid, path, rank);
	}

	free(metadata);
	return nullptr;
}

void reply_stat(int tid, const std::string& path, int rank) {
    auto it = static_home_nodes.find(path);
    if (it != static_home_nodes.end()) {
        std::string node = it->second;
        if (node == node_name) {
#ifdef CAPIOLOG
            logfile << "local stat static home node" << std::endl;
#endif
            handle_local_stat(tid, path);
            return;
        }
    }
	if (files_location.find(path) == files_location.end()) {
		check_file_location(rank, path);
		if (files_location.find(path) == files_location.end()) {
			//if it is in configuration file then wait otherwise fails
			
			if ((metadata_conf.find(path) != metadata_conf.end() || match_globs(path) != -1) && !is_producer(tid, path)) {
				pthread_t t;
				auto* metadata = (struct wait_for_stat_metadata*)  malloc(sizeof(wait_for_stat_metadata));
				metadata->tid = tid;
				strcpy(metadata->path, path.c_str());
				int res = pthread_create(&t, nullptr, wait_for_stat, (void*) metadata);
				if (res != 0) {
					logfile << "error creation of capio server thread wait for stat" << std::endl;
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

	sem_wait(&files_metadata_sem);
	if (files_metadata.find(path) == files_metadata.end()) {
		sem_post(&files_metadata_sem);
		create_file(path, false, file_initial_size);
	}
	else
		sem_post(&files_metadata_sem);

	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path];
	sem_post(&files_metadata_sem);
	std::string mode = c_file.get_mode();
	bool complete = c_file.complete;
	#ifdef CAPIOLOG
		logfile << "node_name : " << node_name << std::endl;
		logfile << " files_location[path]: " << std::get<0>(files_location[path]) << std::endl;
	#endif
	if (complete || strcmp(std::get<0>(files_location[path]), node_name) == 0 || mode == "no_update" || *capio_dir == path) {
		handle_local_stat(tid, path);
	}
	else {
		handle_remote_stat(tid, path, rank);
	}

}

void handle_seek_end(char* str, int rank) {
	int tid, fd;	
	sscanf(str, "send %d %d", &tid, &fd);
	std::string path = processes_files_metadata[tid][fd];
	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path];
	sem_post(&files_metadata_sem);
	// seek_end here behaves as stat because we want the file size
	reply_stat(tid, path, rank);
}

void close_all_files(int tid, int rank) {
	auto it_process_files = processes_files.find(tid);
	if (it_process_files != processes_files.end()) {
		auto process_files = it_process_files->second;
		for (auto it : process_files) {
			handle_close(tid, it.first, rank);
		}
	}
}

/*
 * Unlink resources in shared memory of the thread with thread id = tid
 * To be called only when the client thread terminates
 */

void free_resources(int tid) {
	std::string sem_write_shm_name;
	auto it_resp = response_buffers.find(tid);
	if (it_resp != response_buffers.end()) {
		#ifdef CAPIOLOG
		logfile << "cleaning response buffer " << tid << std::endl;
		#endif
		it_resp->second->free_shm();
		delete it_resp->second;
		response_buffers.erase(it_resp);
	}

	if (sems_write->find(tid) != sems_write->end()) {
		#ifdef CAPIOLOG
		logfile << "cleaning sem_write" << tid << std::endl;
		#endif
		sem_write_shm_name = "sem_write" + std::to_string(tid);
		if (sem_unlink(sem_write_shm_name.c_str()) == -1)
			err_exit("sem_unlink " + sem_write_shm_name + "in sig_term_handler", logfile);
	}

	auto it = data_buffers.find(tid);
	if (it != data_buffers.end()) {
		#ifdef CAPIOLOG
		logfile << "cleaning data buffer " << tid << std::endl;
		#endif
		it->second.first->free_shm();
		it->second.second->free_shm();
		data_buffers.erase(it);
	}
}

void handle_exig(char* str, int rank) {
	int tid;
	sscanf(str, "exig %d", &tid);
	#ifdef CAPIOLOG
	logfile << "handle exit group " << std::endl;
	#endif	
   int pid = pids[tid];
   auto files = writers[pid];
   for (auto& pair : files) {
		std::string path = pair.first;	
   	if (pair.second) {
		auto it_conf = metadata_conf.find(path);
		auto it_app_name = apps.find(tid);
		#ifdef CAPIOLOG
		logfile << "path: " << path << std::endl;
		logfile << "appname exiting " << std::endl;
		if (it_app_name != apps.end()) {
			logfile << it_app_name->second << std::endl;		
		}
		if (it_conf !=metadata_conf.end()) {
			logfile << "appname conf" << std::get<2>(it_conf->second) << std::endl;
		}
		#endif
		if (it_conf == metadata_conf.end() || (std::get<0>(it_conf->second) == "on_termination" && it_app_name != apps.end() && it_app_name->second == std::get<2>(it_conf->second)) || std::get<0>(it_conf->second).empty()) {
			sem_wait(&files_metadata_sem);
			Capio_file& c_file = *files_metadata[path];
			sem_post(&files_metadata_sem);
			#ifdef CAPIOLOG
			logfile << "committed " << c_file.get_committed() << std::endl;
			#endif
			if (c_file.is_dir()) {
					long int n_committed = c_file.n_files_expected;
					#ifdef CAPIOLOG
					logfile << "nfiles in dir " << path << " " << c_file.n_files << std::endl;
					#endif
					if (n_committed <= c_file.n_files) {
						#ifdef CAPIOLOG
						logfile << "dir " << path << " completed " << std::endl;
						#endif
						reply_remote_stats(path);
						c_file.complete = true;
					}
			}
			else {
				#ifdef CAPIOLOG
				logfile << "file " << path << " completed" << std::endl;
				#endif
				c_file.complete = true;
				c_file.commit();
			}
		}
		else {
			#ifdef CAPIOLOG
			logfile << "committed " << std::get<0>(it_conf->second) << " mode " << std::get<1>(it_conf->second) << std::endl;
			#endif
		}
		handle_pending_local_reads(path, false);	
	}
   }
	#ifdef CAPIOLOG
	logfile << "handle exit group 3" << std::endl;
	#endif	
   close_all_files(tid, rank);
   free_resources(tid);
}

void handle_stat(const char* str, int rank) {
	char path[2048];
	int tid;
	sscanf(str, "stat %d %s", &tid, path);
	init_process(tid);
	reply_stat(tid, path, rank);
}

void handle_fstat(const char* str, int rank) {
	int tid, fd;
	sscanf(str, "fsta %d %d", &tid, &fd);
	std::string path = processes_files_metadata[tid][fd];
	#ifdef CAPIOLOG
	logfile << "path " << path << std::endl;
	#endif

	reply_stat(tid, path, rank);
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
	if (it != files_location.end() || metadata_conf.find(path) != metadata_conf.end() || match_globs(path) != -1)
		res = 0;
	else 
		res = -1;
	#ifdef CAPIOLOG
		logfile << "handle access result: " << res << std::endl;
	#endif
	response_buffers[tid]->write(&res);
}


void handle_unlink(const char* str, int rank) {
	char path[PATH_MAX];
	off64_t res;
	int tid;
	#ifdef CAPIOLOG
		logfile << "handle unlink: " << str << std::endl;
	#endif
	sscanf(str, "unlk %d %s", &tid, path);
	sem_wait(&files_metadata_sem);
	auto it = files_metadata.find(path);
	if (it != files_metadata.end()) { //TODO: it works only in the local case
		sem_post(&files_metadata_sem);
		res = 0;
		response_buffers[tid]->write(&res);
		Capio_file& c_file = *(it->second);
		--c_file.n_links;
		#ifdef CAPIOLOG
		logfile << "capio unlink n links " << c_file.n_links << " n opens " << c_file.n_opens;
		#endif
			if (c_file.n_opens == 0 && c_file.n_links <= 0) {
			delete_file(path, rank);
			
		}
	}
	else {
		sem_post(&files_metadata_sem);
		res = -1;
		response_buffers[tid]->write(&res);
	}
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
	for(const std::string& path : parent_files) {
		sem_wait(&files_metadata_sem);
		Capio_file& c_file = *files_metadata[path];
		sem_post(&files_metadata_sem);
		++c_file.n_opens;
	}
}

off64_t create_dir(int tid, const char* pathname, int rank, bool root_dir) {
	off64_t res;
	#ifdef CAPIOLOG
	logfile << "handle mkdir " << pathname << std::endl;
	#endif
	if (files_location.find(pathname) == files_location.end()) {
		std::string shm_name = pathname;
		std::replace(shm_name.begin(), shm_name.end(), '/', '_');
		shm_name = shm_name.substr(1);
		create_file(pathname, true, dir_initial_size);	
		sem_wait(&files_metadata_sem);
		Capio_file& c_file = *files_metadata[pathname];
		if (c_file.first_write) {
			c_file.first_write = false;
			sem_post(&files_metadata_sem);
			//TODO: it works only if there is one prod per file
			if (root_dir) {
				files_location[pathname] =  std::make_pair(node_name, -1);
			}
			else {
            	write_file_location(rank, pathname, tid);
				update_dir(tid, pathname, rank);
			}
				write_entry_dir(tid, pathname, pathname, 1);
				std::string parent_dir = get_parent_dir_path(pathname);
				write_entry_dir(tid, parent_dir, pathname, 2);
        }
		else
			sem_post(&files_metadata_sem);
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
	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path];
	sem_post(&files_metadata_sem);
	++c_file.n_opens;
}

void handle_rename(const char* str, int rank) {
	char oldpath[PATH_MAX];
	char newpath[PATH_MAX];
	int tid;
	off64_t res;
	sscanf(str, "rnam %s %s %d", oldpath, newpath, &tid);

	#ifdef CAPIOLOG
		logfile << "handling rename " << std::endl;
	#endif
	sem_wait(&files_metadata_sem);
	if (files_metadata.find(oldpath) == files_metadata.end())
		res = 1;
	else 
		res = 0;

	sem_post(&files_metadata_sem);
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

	sem_wait(&files_metadata_sem);
	auto node = files_metadata.extract(oldpath);
	node.key() = newpath;
	files_metadata.insert(std::move(node));

	sem_post(&files_metadata_sem);
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
	
    write_file_location(rank, newpath, tid);
	//TODO: remove from files_location oldpath
}


void handle_handshake(const char* str, bool app_name_defined) {
	int tid, pid;
	char app_name[1024];

	if (app_name_defined) {
		sscanf(str, "hand %d %d %s", &tid, &pid, app_name);
		apps[tid] = app_name;
	}
	else
		sscanf(str, "hans %d %d", &tid, &pid);
	pids[tid] = pid;
	init_process(tid);
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
	if (sem_wait(&handle_remote_stat_sem) == -1)
		err_exit("sem_wait handle_remote_stat_sem in handle_remote_stat", logfile);
	auto it = my_remote_pending_stats.find(path_c);
	if (it == my_remote_pending_stats.end()) {
		logfile << "handle_stat_reply " << path_c << " not found, stat already answered for optimization" << std::endl;	
	}
	else {
		for (int tid : it->second) {
			response_buffers[tid]->write(&size);
			response_buffers[tid]->write(&dir);
		}
		my_remote_pending_stats.erase(it);
	}
	if (sem_post(&handle_remote_stat_sem) == -1)
		err_exit("sem_post handle_remote_stat_sem in handle_remote_stat", logfile);
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
		handle_handshake(str, true);
	else if (strncmp(str, "hans", 4) == 0)
		handle_handshake(str, false);
	else if (strncmp(str, "crat", 4) == 0)
		handle_open(str, rank, true);
	else if (strncmp(str, "open", 4) == 0)
		handle_open(str, rank, false);
	else if (strncmp(str, "crax", 4) == 0)
		handle_crax(str, rank);
	else if (strncmp(str, "writ", 4) == 0)
		handle_write(str, rank);
	else if (strncmp(str, "read", 4) == 0)
		handle_read(str, rank, false, false);
	else if (strncmp(str, "clos", 4) == 0)
		handle_close(str, p, rank);
	else if (strncmp(str, "ream", 4) == 0)
		handle_remote_read(str, p, rank);
	else if (strncmp(str, "seek", 4) == 0)
		handle_lseek(str);
	else if (strncmp(str, "sdat", 4) == 0)
		handle_sdat(str);
	else if (strncmp(str, "shol", 4) == 0)
		handle_shol(str);
	else if (strncmp(str, "send", 4) == 0)
		handle_seek_end(str, rank);
	else if (strncmp(str, "exig", 4) == 0)
		handle_exig(str, rank);
	else if (strncmp(str, "stat", 4) == 0)
		handle_stat(str, rank);
	else if (strncmp(str, "stam", 4) == 0)
		handle_stat_reply(str);
	else if (strncmp(str, "fsta", 4) == 0)
		handle_fstat(str, rank);
	else if (strncmp(str, "accs", 4) == 0)
		handle_access(str);
	else if (strncmp(str, "unlk", 4) == 0)
		handle_unlink(str, rank);
	else if (strncmp(str, "clon", 4) == 0)
		handle_clone(str);
	else if (strncmp(str, "mkdi", 4) == 0)
		handle_mkdir(str, rank);
	else if (strncmp(str, "dupp", 4) == 0)
		handle_dup(str);
	else if (strncmp(str, "de64", 4) == 0)
		handle_read(str, rank, true, false);
	else if (strncmp(str, "dent", 4) == 0)
		handle_read(str, rank, true, true);	
	else if (strncmp(str, "rnam", 4) == 0)
		handle_rename(str, rank);
	else {
		logfile << "error msg read" << std::endl;
		MPI_Finalize();
		exit(1);
	}
}

void clean_files_location() {
	std::string file_name;
	for (int rank = 0; rank < n_servers; ++rank) {
		std::string rank_str = std::to_string(rank);
		file_name = "files_location_" + rank_str + ".txt";
		remove(file_name.c_str());
	}
}

void handshake_servers(int rank) {
	char* buf;	
	buf = (char*) malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
	if (!buf)
		err_exit("malloc handshake_servers", logfile);
	if (rank == 0) {
		clean_files_location();
	}
	for (int i = 0; i < n_servers; i += 1) {
		if (i != rank) {
			MPI_Send(node_name, strlen(node_name), MPI_CHAR, i, 0, MPI_COMM_WORLD); //TODO: possible deadlock
			std::fill(buf, buf + MPI_MAX_PROCESSOR_NAME, 0);
			MPI_Recv(buf, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			nodes_helper_rank[buf] = i;
			rank_to_node[i] = buf;
		}
	}
	free(buf);
}

void* capio_server(void* pthread_arg) {
	int rank;
	rank = *(int*)pthread_arg;
	MPI_Comm_size(MPI_COMM_WORLD, &n_servers);
	catch_sigterm();
	handshake_servers(rank);
	open_files_metadata(rank);
	int pid = getpid();
	create_dir(pid, capio_dir->c_str(), rank, true); //TODO: can be a problem if a process execute readdir on capio_dir
	buf_requests = new Circular_buffer<char>("circular_buffer", 1024 * 1024, sizeof(char) * 256);
	if (sem_post(&internal_server_sem) == -1)
		err_exit("sem_post internal_server_sem in capio_server", logfile);
	#ifdef CAPIOLOG
	logfile << "capio dir 2 " << *capio_dir << std::endl;
	#endif	
	while(true) {
		read_next_msg(rank);
		#ifdef CAPIOLOG
		logfile << "after next msg " << std::endl;
		#endif
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

void* wait_for_data(void* pthread_arg) {
	auto* rr_metadata = (struct remote_read_metadata*) pthread_arg;
	const char* path = rr_metadata->path;
	long int offset = rr_metadata->offset;
	int dest = rr_metadata->dest;
	long int nbytes = rr_metadata->nbytes;
	if (sem_wait(rr_metadata->sem) == -1)
		err_exit("sem_wait rr_metadata->sem in wait_for_data", logfile);
	bool complete = *rr_metadata->complete; //warning: possible data races
	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *files_metadata[path];
	sem_post(&files_metadata_sem);
	complete = c_file.complete;
	serve_remote_read(path, dest, offset, nbytes, complete);
	free(rr_metadata->sem);
	free(rr_metadata);
	return nullptr;
}


bool data_avaiable(const char* path_c, long int offset, long int nbytes_requested, long int file_size) {
	return (offset + nbytes_requested <= file_size);
}

int find_batch_size(const std::string& glob) {
		bool found = false;
		int n_files; 
		std::size_t i = 0;

		while (!found && i < metadata_conf_globs.size()) {
			found = glob == std::get<0>(metadata_conf_globs[i]);
			++i;
		}

		if (found)
			n_files = std::get<5>(metadata_conf_globs[i - 1]);
		else
			n_files = -1;

		return n_files;
}

void send_n_files(const std::string& prefix, std::vector<std::string>* files_to_send, int n_files, int dest) {
		std::string msg = "nsend " + prefix;
		#ifdef CAPIOLOG
		logfile << "send_n_files " << std::endl;
		#endif
		size_t prefix_length = prefix.length();
		for (const std::string& path : *files_to_send) {
			msg += " " + path.substr(prefix_length);
			sem_wait(&files_metadata_sem);
			auto it = files_metadata.find(path);
			long int file_size = it->second->get_stored_size();
			sem_post(&files_metadata_sem);
			msg += " " + std::to_string(file_size);
		}
		const char* msg_cstr = msg.c_str();
		#ifdef CAPIOLOG
		logfile << "send_n_files msg " << msg << " strlen " << strlen(msg_cstr) << std::endl;
		#endif
		MPI_Send(msg_cstr, strlen(msg_cstr) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
		void* file_shm;
		for (const std::string& path : *files_to_send) {
			sem_wait(&files_metadata_sem);
			auto it = files_metadata.find(path);
			if (it == files_metadata.end()) {
				#ifdef CAPIOLOG
				logfile << "error send_n_files " << path << " not in files metadata" << std::endl;
				#endif
				exit(1);
			}
			Capio_file& c_file = *(it->second);
			file_shm = c_file.get_buffer();
			long int file_size = c_file.get_stored_size();
			sem_post(&files_metadata_sem);
			#ifdef CAPIOLOG
			logfile << "sending file " << path << " " << file_size << std::endl;
			#endif
			send_file(((char*) file_shm), file_size, dest);
		}
}


void* wait_for_n_files(void* arg) {
	auto* rr_metadata = (struct remote_n_files*) arg;
	#ifdef CAPIOLOG
	logfile << "wait_for_n_files" << std::endl;
	#endif
	if (sem_wait(rr_metadata->sem) == -1)
		err_exit("sem_wait rr_metadata->sem in wait_for_n_files", logfile);
	send_n_files(rr_metadata->prefix, rr_metadata->files_path, rr_metadata->n_files, rr_metadata->dest);
	delete rr_metadata->files_path;
	free(rr_metadata->sem);
	free(rr_metadata);
	#ifdef CAPIOLOG
	logfile << "wait_for_n_files terminating" << std::endl;
	#endif
	return nullptr;
}

std::vector<std::string>* files_avaiable(const std::string& prefix, const std::string& app, const std::string& path_file, int n_files) {
	int n_files_completed = 0;
	size_t prefix_length = prefix.length();
	auto* files_to_send = new std::vector<std::string>;
	std::unordered_set<std::string> &files = files_sent[app];

	sem_wait(&files_metadata_sem);
	auto it_path_file = files_metadata.find(path_file);
	if (it_path_file == files_metadata.end()) {
		sem_post(&files_metadata_sem);
		return files_to_send;
	}
	else {
		Capio_file& c_file = *(it_path_file->second);
		if (c_file.complete) {
			files_to_send->push_back(path_file);
			++n_files_completed;
			files.insert(path_file);
		}
	}
	auto it = files_metadata.begin();	
	sem_post(&files_metadata_sem);
	while (it != files_metadata.end() && n_files_completed < n_files) { // DATA RACE on files_metadata
		std::string path = it->first;
		sem_wait(&files_location_sem);
		auto it_fs = files_location.find(path);
		if (files.find(path) == files.end() && it_fs != files_location.end() && strcmp(std::get<0>(it_fs->second), node_name) == 0 && path.compare(0, prefix_length, prefix) == 0) {
			if (sem_post(&files_location_sem) == -1)
				err_exit("sem_post files_location_sem in files_avaiable", logfile);
			Capio_file &c_file = *(it->second);
			#ifdef CAPIOLOG
			logfile << "path " << path << " to send?" << std::endl;
			#endif
			if (c_file.complete && !c_file.is_dir() ) {
				#ifdef CAPIOLOG
				logfile << "yes path " << path << std::endl;
				#endif
				files_to_send->push_back(path);
				++n_files_completed;
				files.insert(path);
			}
		}
		else {
			if (sem_post(&files_location_sem) == -1)
				err_exit("sem_post files_location_sem in files_avaiable", logfile);
		}
		++it;
	}

	#ifdef CAPIOLOG
	logfile << "files avaiable files sent app " << app << files.size() << std::endl;;
	#endif
	return files_to_send;
}

void helper_nreads_req(char* buf_recv, int dest) {
	char* prefix = (char*) malloc(sizeof(char) * PATH_MAX);
	char* path_file = (char*) malloc(sizeof(char) * PATH_MAX);
	char* app_name = (char*) malloc(sizeof(char) * 512);
	std::size_t n_files;
	sscanf(buf_recv, "nrea %zu %s %s %s", &n_files, app_name, prefix, path_file);
	#ifdef CAPIOLOG
	logfile << "helper_nreads_req n_files " << n_files;
	logfile << " app_name " << app_name << " prefix " << prefix << " path_file " << path_file << std::endl;
	#endif
    n_files = find_batch_size(prefix);
	if (sem_wait(&clients_remote_pending_nfiles_sem) == -1) // important even if not using the data structure
		err_exit("sem_wait clients_remote_pending_nfiles_sem in helper_nreads_req", logfile);

	std::vector<std::string>* files = files_avaiable(prefix, app_name, path_file, n_files);
	if (sem_post(&clients_remote_pending_nfiles_sem) == -1)
		err_exit("sem_post clients_remote_pending_nfiles_sem in helper_nreads_req", logfile);
	if (files->size() == n_files) {
		#ifdef CAPIOLOG
		logfile << "sending n files" << std::endl;
		#endif
		send_n_files(prefix, files, n_files, dest);
		delete files;
	}
	else {
		/* 
		 * create a thread that waits for the completion of such 
		 * files and then send those files
		 */
		pthread_t t;
		auto* rr_metadata = (struct remote_n_files*) malloc(sizeof(struct remote_n_files));
		#ifdef CAPIOLOG
		logfile << "waiting n files" << std::endl;
		#endif
		rr_metadata->prefix = (char*) malloc(sizeof(char) * PATH_MAX);
		if (!rr_metadata->prefix)
			err_exit("malloc 1 in helper_nreads_req", logfile);
		strcpy(rr_metadata->prefix, prefix);
		rr_metadata->dest = dest;
		rr_metadata->sem = (sem_t*) malloc(sizeof(sem_t));
		if (!rr_metadata->sem)
			err_exit("malloc 2 in helper_nreads_req", logfile);
		rr_metadata->files_path = files;
		rr_metadata->n_files = n_files;
		int res = sem_init(rr_metadata->sem, 0, 0);
		if (res == -1)
			err_exit("sem_init rr_metadata->sem in helper_nreads_req", logfile);

		res = pthread_create(&t, nullptr, wait_for_n_files, (void*) rr_metadata);

		if (res != 0) {
			logfile << "error creation of capio server thread wait for completion" << std::endl;
			MPI_Finalize();
			exit(1);
		}
		if (sem_wait(&clients_remote_pending_nfiles_sem) == -1)
			err_exit("sem_wait clients_remote_pending_nfiles_sem in helper_nreads_req", logfile);
		clients_remote_pending_nfiles[app_name].push_back(rr_metadata);
		if (sem_post(&clients_remote_pending_nfiles_sem) == -1)
			err_exit("sem_post clients_remote_pending_nfiles_sem in helper_nreads_req", logfile);
	}
	free(prefix);
	free(path_file);
	free(app_name);
}

void lightweight_MPI_Recv(char* buf_recv, int buf_size, MPI_Status* status) {
	MPI_Request request;
	int received = 0;
	#ifdef CAPIOLOG
		logfile << "MPI_Irecv debug 0" << std::endl;
	#endif
	MPI_Irecv(buf_recv, buf_size, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &request); //receive from server
	#ifdef CAPIOLOG
		logfile << "MPI_Irecv debug 1" << std::endl;
	#endif
	struct timespec sleepTime;
    struct timespec returnTime;
    sleepTime.tv_sec = 0;
    sleepTime.tv_nsec = 200000;

	while (!received) {
		MPI_Test(&request, &received, status);
		nanosleep(&sleepTime, &returnTime);
	}
	int bytes_received;
	MPI_Get_count(status, MPI_BYTE, &bytes_received);
	#ifdef CAPIOLOG
		logfile << "MPI_Irecv debug 2 bytes received " << bytes_received << std::endl;
	#endif	
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
		MPI_Recv(shm + k, count, MPI_BYTE, source, 0, MPI_COMM_WORLD, &status);
    #ifdef CAPIOLOG
		logfile << "debug1" << std::endl;
#endif
		MPI_Get_count(&status, MPI_BYTE, &bytes_received);
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
			#ifdef CAPIOLOG
				logfile << "wait for completion before 0" << std::endl;
			#endif
	auto* rr_metadata = (struct remote_stat_metadata*) pthread_arg;
	const char* path = rr_metadata->path;
	Capio_file* c_file = rr_metadata->c_file;
			#ifdef CAPIOLOG
				logfile << "wait for completion before" << std::endl;
			#endif
	if (sem_wait(rr_metadata->sem) == -1)
		err_exit("sem_wait rr_metadata->sem in wait_for_completion", logfile);
	serve_remote_stat(path, rr_metadata->dest, *c_file);
	free(rr_metadata->path);
	free(rr_metadata->sem);
	free(rr_metadata);
			#ifdef CAPIOLOG
				logfile << "wait for completion after" << std::endl;
			#endif
	return nullptr;
}

void helper_stat_req(const char* buf_recv) {
	char* path_c = (char*) malloc(sizeof(char) * PATH_MAX);
	if (!path_c)
		err_exit("malloc 1 in helper_stat_req", logfile);
	int dest;
	sscanf(buf_recv, "stat %d %s", &dest, path_c);
	//std::string path(path_c);
	//for (auto p : files_metadata) {
//		logfile << "files metadata " << p.first << std::endl;
//	}
	sem_wait(&files_metadata_sem);
	Capio_file& c_file = *(files_metadata[path_c]);

	sem_post(&files_metadata_sem);
	if (c_file.complete) {
		serve_remote_stat(path_c, dest, c_file);
	}
	else { //wait for completion

  pthread_t t;
  auto* rr_metadata = (struct remote_stat_metadata*) malloc(sizeof(struct remote_stat_metadata));

	rr_metadata->path = (char*) malloc(sizeof(char) * PATH_MAX);
	strcpy(rr_metadata->path, path_c);

	rr_metadata->c_file = &c_file;
	rr_metadata->dest = dest;
	rr_metadata->sem = (sem_t*) malloc(sizeof(sem_t));
	if (!rr_metadata->sem)
		err_exit("malloc 2 in helper_stat_req", logfile);
	int res = sem_init(rr_metadata->sem, 0, 0);

  if (res == -1) 
	  err_exit("sem_init rr_metadata->sem in helper_stat_req", logfile);
	#ifdef CAPIOLOG
	logfile << "before pthread_create" << std::endl;
	#endif
	res = pthread_create(&t, nullptr, wait_for_completion, (void*) rr_metadata);
	#ifdef CAPIOLOG
	logfile << "after pthread create" << std::endl;
	#endif
	if (res != 0) {
	  logfile << "error creation of capio server thread wait for completion" << std::endl;
		MPI_Finalize();
		exit(1);
	}
	clients_remote_pending_stat[path_c].push_back(rr_metadata->sem);
	}
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

void recv_nfiles(const char* buf_recv, int source) {
	std::string path, bytes_received, prefix;
	std::vector<std::pair<std::string, std::string>> files;
	std::stringstream input_stringstream(buf_recv);
	getline(input_stringstream, path, ' ');
	getline(input_stringstream, prefix, ' ');
	#ifdef CAPIOLOG
	logfile << "recv_nfiles prefix " << prefix << std::endl;
	#endif
	while (getline(input_stringstream, path, ' ')) {
		#ifdef CAPIOLOG
		logfile << "recv_nfiles path " << path << std::endl;
		#endif
		path = prefix + path;
		getline(input_stringstream, bytes_received, ' ');
		files.emplace_back(path, bytes_received);
		void* p_shm;
		Capio_file* c_file;
		sem_wait(&files_metadata_sem);
		auto it = files_metadata.find(path);
		long int file_size = std::stoi(bytes_received);
		if (it == files_metadata.end()) {			
			sem_post(&files_metadata_sem);
			#ifdef CAPIOLOG
			logfile << "creating new file " << path <<  std::endl;
			#endif
			std::string node_name = rank_to_node[source];
			const char* node_name_str = node_name.c_str();
			char* p_node_name = (char*) malloc(sizeof(char) * (strlen(node_name_str) + 1));	
			strcpy(p_node_name, node_name_str);
			files_location[path] = std::make_pair(p_node_name, -1);
			p_shm = new char[file_size];
			create_file(path, false, file_size);
			sem_wait(&files_metadata_sem);
			it = files_metadata.find(path);
			if (it == files_metadata.end()) {
				std::cerr << "error recv nfiles" << std::endl;
				exit(1);
			}
			c_file = (it->second);
			c_file->insert_sector(0, file_size);
			c_file->complete = true;
			c_file->real_file_size = file_size;
			sem_post(&files_metadata_sem);
		}
		else {
			sem_post(&files_metadata_sem);
			c_file = (it->second);
			if (c_file->buf_to_allocate()) {
        #ifdef CAPIOLOG
        logfile << "allocating file " << path << std::endl;
        #endif
				c_file->create_buffer(path, false);
}
			p_shm = c_file->get_buffer();
			#ifdef CAPIOLOG
			logfile << "file was already created " << path <<  std::endl;
			#endif

			off64_t file_shm_size = c_file->get_buf_size();

			if (file_size > file_shm_size) {
				p_shm = expand_memory_for_file(file_size, *c_file);
			}
		}
		#ifdef CAPIOLOG
			logfile << "receiving file " << path << " bytes to receive " << bytes_received << std::endl;
		#endif

		recv_file((char*) p_shm, source, file_size);
		c_file->first_write = false;
		

	}
	for (const auto& pair : files) {
		std::string file_path = pair.first;
		std::string bytes_received = pair.second;
		#ifdef CAPIOLOG
			logfile << " checking remote pending reads for file " << file_path << std::endl;
		#endif
		if (my_remote_pending_reads.find(file_path) != my_remote_pending_reads.end()) {		
		#ifdef CAPIOLOG
			logfile << " solving remote pending reads for file " << file_path << std::endl;
		#endif
			//std::string msg = "ream " + file_path + " " + bytes_received + " " + std::to_string(0) + " " + std::to_string(complete) + " " + bytes_received;
			solve_remote_reads(std::stol(bytes_received), 0, std::stol(bytes_received), file_path.c_str(), true);
		}
	}
}

void* capio_helper(void* pthread_arg) {
	size_t buf_size = sizeof(char) * (PATH_MAX + 81920);
	char* buf_recv = (char*) malloc(buf_size);
	if (!buf_recv)
		err_exit("malloc 1 in capio_helper", logfile);
	MPI_Status status;
	sem_wait(&internal_server_sem);
	while(true) {
		#ifdef CAPIOLOG
		logfile << " beginning helper loop " << std::endl;
		#endif
		#ifdef CAPIOSYNC
		MPI_Recv(buf_recv, buf_size, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status); //receive from server
		#else
		lightweight_MPI_Recv(buf_recv, buf_size, &status); //receive from server
		#endif
		int source = status.MPI_SOURCE;
		bool remote_request_to_read = strncmp(buf_recv, "read", 4) == 0;
		if (remote_request_to_read) {
		#ifdef CAPIOLOG
		logfile << "helper remote req to read " << buf_recv << std::endl;
		#endif
		    // schema msg received: "read path dest offset nbytes"
			char* path_c = (char*) malloc(sizeof(char) * PATH_MAX);
			if (!path_c)
				err_exit("malloc 2 in capio_helper", logfile);
			int dest;
			long int offset, nbytes;
			sscanf(buf_recv, "read %s %d %ld %ld", path_c, &dest, &offset, &nbytes);
			
			//check if the data is avaiable
			sem_wait(&files_metadata_sem);
			auto it = files_metadata.find(path_c);
			Capio_file& c_file = *(it->second);
			long int file_size = c_file.get_stored_size();
			sem_post(&files_metadata_sem);
			bool complete = c_file.complete; 
			if (complete || (c_file.get_mode() == "no_update" && data_avaiable(path_c, offset, nbytes, file_size))) {
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
				auto* rr_metadata = (struct remote_read_metadata*) malloc(sizeof(struct remote_read_metadata));
				if (!rr_metadata)
					err_exit("malloc 3 in capio_helper", logfile);
				strcpy(rr_metadata->path, path_c);
				rr_metadata->offset = offset;
				rr_metadata->dest = dest;
				rr_metadata->nbytes = nbytes;
				rr_metadata->complete = &(c_file.complete);
				rr_metadata->sem = (sem_t*) malloc(sizeof(sem_t));
				if (!rr_metadata->sem)
					err_exit("malloc 4 in capio_helper", logfile);
				int res = sem_init(rr_metadata->sem, 0, 0);
				if (res == -1)
					err_exit("sem_init rr_metadata->sem", logfile); 
				res = pthread_create(&t, nullptr, wait_for_data, (void*) rr_metadata);
				if (res != 0) {
					logfile << "error creation of capio server thread helper loop wait for data" << std::endl;
					MPI_Finalize();
					exit(1);
				}
				clients_remote_pending_reads[path_c].push_back(std::make_tuple(offset, nbytes, rr_metadata->sem));
			}
			free(path_c);
		}
		else if(strncmp(buf_recv, "sending", 7) == 0) { //receiving a file
			#ifdef CAPIOLOG
				logfile << "helper received sending msg " << buf_recv << std::endl;
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
			sem_wait(&files_metadata_sem);
			auto it = files_metadata.find(path);
            if (it == files_metadata.end()) {
                sem_post(&files_metadata_sem);
                create_file(path, false, file_initial_size);
                sem_wait(&files_metadata_sem);
                it = files_metadata.find(path);
            }
            Capio_file &c_file = *(it->second);
            if (c_file.buf_to_allocate()) {

#ifdef CAPIOLOG
                logfile << "allocating file " << path << std::endl;
#endif
                c_file.create_buffer(path, false);
            }
            file_shm = c_file.get_buffer();
            c_file.complete = true;
            sem_post(&files_metadata_sem);
			#ifdef CAPIOLOG
				logfile << "helper before received part of the file from process " << source << std::endl;
				logfile << "offset " << offset << std::endl;
				logfile << "bytes received " << bytes_received << std::endl;
			#endif
			if (bytes_received != 0) {

				Capio_file& c_file = *(it->second);
				off64_t file_shm_size = c_file.get_buf_size();
				off64_t file_size = offset + bytes_received;
				if (file_size > file_shm_size) {
					file_shm = expand_memory_for_file(file_size, c_file);
				}
				recv_file((char*)file_shm + offset, source, bytes_received);
			#ifdef CAPIOLOG
				logfile << "helper received part of the file" << std::endl;
			#endif
				bytes_received *= sizeof(char);
			}

			solve_remote_reads(bytes_received, offset, file_size, path.c_str(), complete);
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
		else if(strncmp(buf_recv, "nrea", 4) == 0) {
			helper_nreads_req(buf_recv, source);
		}
		else if(strncmp(buf_recv, "nsend", 5) == 0) { 
				recv_nfiles(buf_recv, source);
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

void update_metadata_conf(std::string& path, size_t pos, long int n_files, size_t batch_size, const std::string& committed,
const std::string& mode, const std::string& app_name, bool permanent, long int n_close) {
	if (pos == std::string::npos && n_files == -1) {
		metadata_conf[path] = std::make_tuple(committed, mode, app_name, n_files, permanent, n_close);
		#ifdef CAPIOLOG
		logfile << "path " << path << " app name " << app_name << " n_files " << n_files << std::endl;
		#endif
	}
	else {
		std::string prefix_str = path.substr(0, pos);
		// if pos == std::string::npos it means prefix_str = path
		metadata_conf_globs.emplace_back(prefix_str, committed, mode, app_name, n_files, batch_size, permanent, n_close);
		#ifdef CAPIOLOG
		logfile << "prefix_str " << prefix_str << " app name " << app_name << " batch size " << batch_size << std::endl;
		#endif
	}
}

bool is_int(const std::string& s) {
	bool res = false;
	if (!s.empty()) {
		char* p;
		strtol(s.c_str(), &p, 10);
		res = *p == 0;
	}
	return res;
}

void parse_conf_file(const std::string& conf_file) {
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
	entries["name"];	
	auto io_graph = entries["IO_Graph"];
	for (auto app : io_graph) {
		std::string_view app_name = app["name"].get_string();
		#ifdef CAPIOLOG
		logfile << "app_name " << app_name << std::endl;
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
				std::string committed_str;
				error = file["committed"].get_string().get(committed);
				std::string commit_rule;
				long int n_close = -1;
				if (!error) {
					#ifdef CAPIOLOG
					logfile << " committed " << committed << std::endl;
					#endif
					committed_str = std::string(committed);
					int pos = committed_str.find(':'); 
					if (pos != -1) {
						commit_rule = committed_str.substr(0, pos);
						if (commit_rule != "on_close") {
							logfile << "error conf file: commit rule " << commit_rule << std::endl;
							exit(1);
						}
						std::string n_close_str = committed_str.substr(pos + 1, committed_str.length());
			#ifdef CAPIOLOG
			logfile << "conf on_close_n_str " << n_close_str << std::endl;
			#endif	
   						if (!is_int(n_close_str)) {
							logfile << "error conf file:  commit rule on_close invalid number" << std::endl;
							exit(1);
						}
						n_close = std::stol(n_close_str);
					}
					else
						commit_rule = std::string(committed);
				}
				else {
					logfile << "error conf file: commit rule is mandatory in streaming section" << std::endl;
					exit(1);
				}

			#ifdef CAPIOLOG
			logfile << "conf on_close_n " << n_close << std::endl;
			#endif	
				std::string_view mode;
				error = file["mode"].get_string().get(mode);
				if (!error) {
					#ifdef CAPIOLOG
					logfile << " mode " << mode << std::endl;
					#endif
				}
				long int n_files;
				error = file["n_files"].get_int64().get(n_files);
				if (error)
					n_files = -1;
				else {
					#ifdef CAPIOLOG
					logfile << " n_files " << n_files << std::endl;
					#endif
				}
				long int batch_size;
				error = file["batch_size"].get_int64().get(batch_size);
				if (error)
					batch_size = 0;
				std::string path = std::string(name);
				if (!is_absolute(path.c_str())) {
					if (path.substr(0, 2) == "./") 
						path = path.substr(2, path.length() - 2);
					if (path == ".")
						path = *capio_dir;
					else
						path = *capio_dir + "/" + path;
				}
				std::size_t pos = path.find('*');
				update_metadata_conf(path, pos, n_files, batch_size, std::string(commit_rule), std::string(mode), std::string(app_name), false, n_close);
			}
		}
	}
	ondemand::array permanent_files;
	auto error = entries["permanent"].get_array().get(permanent_files);
	long int batch_size = 0;
	if (!error) {
		for (auto file : permanent_files) {
			std::string_view name;
			error = file.get_string().get(name);
			std::string path = std::string(name);
			#ifdef CAPIOLOG
			logfile << "permanent file: " << path << std::endl;
			#endif
			if (!is_absolute(path.c_str())) {
				if (path.substr(0, 2) == "./") 
					path = path.substr(2, path.length() - 2);
				path = *capio_dir + "/" + path;
			}
			if (!is_absolute(path.c_str())) {
				if (path.substr(0, 2) == "./") 
					path = path.substr(2, path.length() - 2);
				path = *capio_dir + "/" + path;
			}
			std::size_t pos = path.find('*');	
			if (pos == std::string::npos) {
				auto it = metadata_conf.find(path);
				if (it == metadata_conf.end())
					update_metadata_conf(path, pos, -1, batch_size, "on_termination", "", "", true, -1);
				else
					std::get<4>(it->second) = true;
			}
			else {
				std::string prefix_str = path.substr(0, pos);
				long int i = match_globs(prefix_str);
				if (i == -1)
					update_metadata_conf(path, pos, -1, batch_size, "on_termination", "", "", true, -1);
				else {
					auto& tuple = metadata_conf_globs[i];
					std::get<6>(tuple) = true;
				}
			}
		}
	}
}


void get_capio_dir() {
	char* val;
	if (capio_dir == nullptr) {
		val = getenv("CAPIO_DIR");
		try {
			if (!val) {
				capio_dir = new std::string(std::filesystem::canonical("."));	
			}
			else {
				capio_dir = new std::string(std::filesystem::canonical(val));
			}
		}
		catch (const std::exception& ex) {
			if (!val)
				logfile << "error CAPIO_DIR: current directory not valid" << std::endl;
			else
				logfile << "error CAPIO_DIR: directory " << val << " does not exist" << std::endl; 
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
	if (val) {
		PREFETCH_DATA_SIZE = strtol(val, nullptr, 10);
	}
	#ifdef CAPIOLOG
	logfile << "PREFETCH_DATA_SIZE " << PREFETCH_DATA_SIZE << std::endl;
	#endif	
}

void get_file_initial_size() {
	char* val;
	val = getenv("CAPIO_FILE_INIT_SIZE");
	if (val) {
		file_initial_size = strtol(val, nullptr, 10);
	}
	#ifdef CAPIOLOG
	logfile << "FILE_INITIAL_SIZE " << file_initial_size << std::endl;
	#endif	
}

int main(int argc, char** argv) {
	int rank, len, provided;
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	std::string conf_file;
	if (argc < 2 || argc > 3) {
		std::cerr << "input error: ./capio_server server_log_path [conf_file]" << std::endl;
		exit(1);
	}
	std::string server_log_path = argv[1];
  	logfile.open (server_log_path + "_" + std::to_string(rank), std::ofstream::out);
	get_capio_dir();
	get_circular_buffers_info();
	#ifdef CAPIOLOG
	logfile << "WINDOW_DATA_BUFS " << *WINDOW_DATA_BUFS << " N_ELEMS_DATA_BUFS " << * N_ELEMS_DATA_BUFS << std::endl;
	#endif	
	get_prefetch_data_size();
	get_file_initial_size();
	if (argc == 3) {
		conf_file = argv[2];
		parse_conf_file(conf_file);
	}
	sems_write = new std::unordered_map<int, sem_t*>;
    if	(provided != MPI_THREAD_MULTIPLE) {
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
	if (sem_init(&remote_read_sem, 0, 1) == -1)
		err_exit("sem_init remote_read_sem in main", logfile);
	if (sem_init(&handle_remote_read_sem, 0, 1) == -1)
		err_exit("sem_init handle_remote_read_sem in main", logfile);
	if (sem_init(&handle_remote_stat_sem, 0, 1) == -1)
		err_exit("sem_init handle_remote_stat_sem in main", logfile);
	if (sem_init(&handle_local_read_sem, 0, 1) == -1)
		err_exit("sem_init handle_local_read_sem in main", logfile);
	if (sem_init(&files_metadata_sem, 0, 1) == -1)
		err_exit("sem_init files_metadata_sem in main", logfile);
	if (sem_init(&files_location_sem, 0, 1) == -1)
		err_exit("sem_init files_location_sem in main", logfile);
	if (sem_init(&clients_remote_pending_nfiles_sem, 0, 1) == -1)
		err_exit("sem_init clients_remote_pending_nfiles_sem in main", logfile);
	if (res !=0) {
		logfile << __FILE__ << ":" << __LINE__ << " - " << std::flush;
		perror("sem_init failed"); exit(res);
	}

    static_home_nodes.emplace("/beegfs/home/albemart/capio_broadwell/motexample/file0.dat", "broadwell-003");

    static_home_nodes.emplace("/beegfs/home/albemart/capio_broadwell/motexample/file1.dat", "broadwell-004");
    static_home_nodes.emplace("/beegfs/home/albemart/capio_broadwell/motexample/file3.dat", "broadwell-004");
    static_home_nodes.emplace("/beegfs/home/albemart/capio_broadwell/motexample/file4.dat", "broadwell-004");

    static_home_nodes.emplace("/beegfs/home/albemart/capio_broadwell/motexample/file2.dat", "broadwell-005");
    static_home_nodes.emplace("/beegfs/home/albemart/capio_broadwell/motexample/file5.dat", "broadwell-005");
    static_home_nodes.emplace("/beegfs/home/albemart/capio_broadwell/motexample/output_W0.dat", "broadwell-005");
    static_home_nodes.emplace("/beegfs/home/albemart/capio_broadwell/motexample/output_W1.dat", "broadwell-005");
    static_home_nodes.emplace("/beegfs/home/albemart/capio_broadwell/motexample/output_X.dat", "broadwell-005");
    static_home_nodes.emplace("/beegfs/home/albemart/capio_broadwell/motexample/output_X.dat", "broadwell-005");

	res = pthread_create(&server_thread, nullptr, capio_server, &rank);
	if (res != 0) {
		logfile << "error creation of capio server main thread" << std::endl;
		MPI_Finalize();
		return 1;
	}
	res = pthread_create(&helper_thread, nullptr, capio_helper, nullptr);
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
	if (res !=0) {
		logfile << __FILE__ << ":" << __LINE__ << " - " << std::flush;
		perror("sem_destroy failed"); exit(res);
	}
	MPI_Finalize();
	return 0;
}
