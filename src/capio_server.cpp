#include "circular_buffer.hpp"

#include <string>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <iostream>
#include <pthread.h>
#include <sstream>

#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>

#include <mpi.h>

#include "utils/common.hpp"

const long int max_shm_size = 1024L * 1024 * 1024 * 16;
bool shm_full = false;
long int total_bytes_shm = 0;

// pid -> fd ->(file_shm, index)
std::unordered_map<int, std::unordered_map<int, std::pair<void*, long int>>> processes_files;

// pid -> fd -> pathname
std::unordered_map<int, std::unordered_map<int, std::string>> processes_files_metadata;

// pid -> (response shared buffer, index)
std::unordered_map<int, Circular_buffer<size_t>*> response_buffers;

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

// pid -> (response shared buffer, size)
std::unordered_map<int, std::pair<int*, int*>> caching_info;

// pathname -> (file_shm, file_size)
std::unordered_map<std::string, std::pair<void*, size_t*>> files_metadata;

// pathname -> node
std::unordered_map<std::string, char*> files_location;

// node -> rank
std::unordered_map<std::string, int> nodes_helper_rank;

/*
 *
 * It contains all the reads requested by local processes to read files that are in the local node for which the data is not yet avaiable.
 * path -> [(pid, fd, numbytes), ...]
 *
 */

std::unordered_map<std::string, std::vector<std::tuple<int, int, long int>>>  pending_reads;

/*
 *
 * It contains all the reads requested to the remote nodes that are not yet satisfied 
 * path -> [(pid, fd, numbytes), ...]
 *
 */

std::unordered_map<std::string, std::list<std::tuple<int, int, long int>>>  my_remote_pending_reads;

/*
 *
 * It contains all the read requested by other nodes for which the data is not yet avaiable 
 * path -> [(offset, numbytes, sem_pointer), ...]
 *
 */

std::unordered_map<std::string, std::list<std::tuple<long int, long int, sem_t*>>> clients_remote_pending_reads;

// it contains the file saved on disk
std::unordered_set<std::string> on_disk;


//name of the node

char node_name[MPI_MAX_PROCESSOR_NAME];

Circular_buffer<char>* buf_requests; 
std::unordered_map<int, sem_t*> sems_response;
std::unordered_map<int, sem_t*> sems_write;

sem_t internal_server_sem;

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
	//free all the memory used
	for (auto& pair : files_metadata) {
		shm_unlink(pair.first.c_str());
		shm_unlink((pair.first + "_size").c_str());
	}
	for (auto& pair : response_buffers) {
		pair.second->free_shm();
		delete pair.second;
		sem_unlink(("sem_response_read" + std::to_string(pair.first)).c_str());
		sem_unlink(("sem_response_write" + std::to_string(pair.first)).c_str());
		sem_unlink(("sem_write" + std::to_string(pair.first)).c_str());
		shm_unlink(("caching_info" + std::to_string(pair.first)).c_str()); 
		shm_unlink(("caching_info_size" + std::to_string(pair.first)).c_str()); 
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

void* create_shm_circular_buffer(std::string shm_name) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
	const long int size = 1024L * 1024 * 1024;
	if (fd == -1)
		err_exit("shm_open");
	if (ftruncate(fd, size) == -1)
		err_exit("ftruncate");
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap create_shm_circular_buffer");
//	if (close(fd) == -1);
//		err_exit("close");
	return p;
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
size_t* create_shm_size_t(std::string shm_name) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
	const int size = sizeof(size_t);
	if (fd == -1)
		err_exit("shm_open");
	if (ftruncate(fd, size) == -1)
		err_exit("ftruncate");
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap create_shm_size_t");
//	if (close(fd) == -1);
//		err_exit("close");
	return (size_t*) p;
}

void write_file_location(const std::string& file_name, int rank, std::string path_to_write) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    int fd;
    if ((fd = open(file_name.c_str(), O_WRONLY|O_APPEND, 0664)) == -1) {
        std::cerr << "writer " << rank << " error opening file, errno = " << errno << " strerror(errno): " << strerror(errno) << std::endl;
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
        std::cerr << "write " << rank << "failed to lock the file" << std::endl;
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
	files_location[path_to_write] = node_name;
	// Now release the lock explicitly.
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLKW, &lock) == - 1) {
        std::cerr << "write " << rank << "failed to unlock the file" << std::endl;
    }
	free(file_location);
    close(fd); // close the file: would unlock if needed
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
		std::cerr << "capio server " << rank << " failed to open the location file" << std::endl;
		return false;
	}
	fd = fileno(fp);
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        std::cerr << "capio server " << rank << " failed to lock the file" << std::endl;
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
		files_location[path_to_check] = (char*) malloc(sizeof(node_str) + 1); //TODO:free the memory
		if (strcmp(path, path_to_check_cstr) == 0) {
			found = true;
			strcpy(files_location[path_to_check], node_str);
		}
		//check if the file is present
    }
	res = found;
    /* Release the lock explicitly. */
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        std::cerr << "reader " << rank << " failed to unlock the file" << std::endl;
        res = false;
    }
    fclose(fp);
    return res;
}

void flush_file_to_disk(int pid, int fd) {
	void* buf = processes_files[pid][fd].first;
	std::string path = processes_files_metadata[pid][fd];
	long int file_size = *files_metadata[path].second;	
	long int num_bytes_written, k = 0;
	while (k < file_size) {
    	num_bytes_written = write(fd, ((char*) buf) + k, (file_size - k));
    	k += num_bytes_written;
    }
}

//TODO: function too long

void handle_open(char* str, char* p, int rank) {
	#ifdef CAPIOLOG
	std::cout << "handle open" << std::endl;
	#endif
	int pid;
	pid = strtol(str + 5, &p, 10);
	if (sems_response.find(pid) == sems_response.end()) {
		sems_response[pid] = sem_open(("sem_response_read" + std::to_string(pid)).c_str(), O_RDWR);
		if (sems_response[pid] == SEM_FAILED) {
			err_exit("error creating sem_response_read" + std::to_string(pid));  	
		}
		sems_write[pid] = sem_open(("sem_write" + std::to_string(pid)).c_str(), O_RDWR);
		if (sems_write[pid] == SEM_FAILED) {
			err_exit("error creating sem_write" + std::to_string(pid));
		}
		Circular_buffer<size_t>* cb = new Circular_buffer<size_t>("buf_response" + std::to_string(pid), 256L * 1024 * 1024, sizeof(long int));
		response_buffers.insert({pid, cb});
		caching_info[pid].first = (int*) get_shm("caching_info" + std::to_string(pid));
		caching_info[pid].second = (int*) get_shm("caching_info_size" + std::to_string(pid));
	}
	std::string path(p + 1);
	int fd = open(path.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IRWXU);
	if (fd == -1) {
	std::cerr << "capio server, error to open the file " << path << std::endl;
	MPI_Finalize();
	exit(1);
	}
	void* p_shm;
	int index = *caching_info[pid].second;
	caching_info[pid].first[index] = fd;
	if (on_disk.find(path) == on_disk.end()) {
		p_shm = create_shm(path, 1024L * 1024 * 1024* 6);
		caching_info[pid].first[index + 1] = 0;
	}
	else {
		p_shm = nullptr;
		caching_info[pid].first[index + 1] = 1;
	}
		//TODO: check the size that the user wrote in the configuration file
	processes_files[pid][fd] = std::pair<void*, int>(p_shm, 0); //TODO: what happens if a process open the same file twice?
	*caching_info[pid].second += 2;
	size_t fdt = fd;
	response_buffers[pid]->write(&fdt);
	if (files_metadata.find(path) == files_metadata.end()) {
		files_metadata[path].first = processes_files[pid][fd].first;	
		files_metadata[path].second = create_shm_size_t(path + "_size");	

		if (files_metadata.find(path) == files_metadata.end()) {//debug
			std::cerr << "server " << rank << " error updating" <<std ::endl;
			exit(1);
		}
	}
	processes_files_metadata[pid][fd] = path;
}

void handle_pending_read(int pid, int fd, long int process_offset, long int count) {
	#ifdef MYDEBUG
	int* tmp = (int*) malloc(count);
	memcpy(tmp, processes_files[pid][fd].first + process_offset, count); 
	for (int i = 0; i < count / sizeof(int); ++i) {
		std::cout << "server local read tmp[i] " << tmp[i] << std::endl;
	}
	free(tmp);
	#endif
	std::string path = processes_files_metadata[pid][fd];
	response_buffers[pid]->write(files_metadata[path].second);
	processes_files[pid][fd].second += count;
	//TODO: check if the file was moved to the disk

}

struct handle_write_metadata{
	char str[64];
	long int rank;
};

void handle_write(const char* str, int rank) {
        #ifdef CAPIOLOG
        std::cout << "handle write" << std::endl;
        #endif
        //check if another process is waiting for this data
        std::string request;
        int pid, fd;
        long int data_size;
        std::istringstream stream(str);
        stream >> request >> pid >> fd >> data_size;
        if (processes_files[pid][fd].second == 0) {
                write_file_location("files_location.txt", rank, processes_files_metadata[pid][fd]);
        }
        processes_files[pid][fd].second = data_size;
        std::string path = processes_files_metadata[pid][fd];
        *files_metadata[path].second = data_size; //works only if there is only one writer at time      for each file
        /*total_bytes_shm += data_size;
        if (total_bytes_shm > max_shm_size && on_disk.find(path) == on_disk.end()) {
                shm_full = true;
                flush_file_to_disk(pid, fd);
                int i = 0;
                bool found = false;
                while (!found && i < *caching_info[pid].second) {
                        if (caching_info[pid].first[i] == fd) {
                                found = true;
                        }
                        else {
                                i += 2;
								                       }
                }
                if (i >= *caching_info[pid].second) {
                        MPI_Finalize();
                        exit(1);
                }
                if (found) {
                        caching_info[pid].first[i + 1] = 1;
                }
                on_disk.insert(path);
        }*/
        //sem_post(sems_response[pid]);
        auto it = pending_reads.find(path);
        sem_wait(sems_write[pid]);
        if (it != pending_reads.end()) {
                auto& pending_reads_this_file = it->second;
                int i = 0;
                for (auto it_vec = pending_reads_this_file.begin(); it_vec != pending_reads_this_file.end(); it++) {
                        auto tuple = *it_vec;
                        int pending_pid = std::get<0>(tuple);
                        int fd = std::get<1>(tuple);
                        long int process_offset = processes_files[pending_pid][fd].second;
                        long int count = std::get<2>(tuple);
                        long int file_size = *files_metadata[path].second;
                        if (process_offset + count <= file_size) {
                                handle_pending_read(pending_pid, fd, process_offset, count);
                        }
                        pending_reads_this_file.erase(it_vec);
                        ++i;
                }
        }
        auto it_client = clients_remote_pending_reads.find(path);
		std::list<std::tuple<long int, long int, sem_t*>>::iterator it_list, prev_it_list;
        if (it_client !=  clients_remote_pending_reads.end()) {
                while (it_list != it_client->second.end()) {
                        long int offset = std::get<0>(*it_list);
                        long int nbytes = std::get<1>(*it_list);
                        sem_t* sem = std::get<2>(*it_list);
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
}


/*
 * Multithread function
 *
 */

void handle_local_read(int pid, int fd, size_t count) {
		#ifdef CAPIOLOG
		std::cout << "handle local read" << std::endl;
		#endif
		std::string path = processes_files_metadata[pid][fd];
		size_t file_size = *files_metadata[path].second;
		size_t process_offset = processes_files[pid][fd].second;
		if (process_offset + count > file_size) {
			pending_reads[path].push_back(std::make_tuple(pid, fd, count));
			return;
		}
#ifdef MYDEBUG
		int* tmp = (int*) malloc(count);
		memcpy(tmp, processes_files[pid][fd].first + process_offset, count); 
		for (int i = 0; i < count / sizeof(int); ++i) {
			if (tmp[i] != i % 10) 
				std::cerr << "server local read tmp[i] " << tmp[i] << std::endl;
		}
		free(tmp);
#endif
		response_buffers[pid]->write(&file_size);
		#ifdef CAPIOLOG
		std::cout << "process offset " << process_offset << std::endl;
		#endif
		processes_files[pid][fd].second += count;
		//TODO: check if the file was moved to the disk
}

/*
 * Multithread function
 *
 */

void handle_remote_read(int pid, int fd, long int count, int rank) {
		#ifdef CAPIOLOG
		std::cout << "handle remote read" << std::endl;
		#endif
		const char* msg;
		std::string str_msg;
		int dest = nodes_helper_rank[files_location[processes_files_metadata[pid][fd]]];
		long int offset = processes_files[pid][fd].second;
		str_msg = "read " + processes_files_metadata[pid][fd] + " " + std::to_string(rank) + " " + std::to_string(offset) + " " + std::to_string(count); 
		msg = str_msg.c_str();
		MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
		my_remote_pending_reads[processes_files_metadata[pid][fd]].push_back(std::make_tuple(pid, fd, count));
}

struct wait_for_file_metadata{
	int pid;
	int fd;
	size_t count;
};

void* wait_for_file(void* pthread_arg) {
	struct wait_for_file_metadata* metadata = (struct wait_for_file_metadata*) pthread_arg;
	int pid = metadata->pid;
	int fd = metadata-> fd;
	size_t count = metadata->count;
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
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int fd_locations; /* file descriptor to identify a file within a process */

	
	
	std::string path_to_check(processes_files_metadata[pid][fd]);
	const char* path_to_check_cstr = path_to_check.c_str();

	while (!found) {
		sleep(1);
		int tot_chars = 0;
    	fp = fopen("files_location.txt", "r");
		if (fp == NULL) {
			MPI_Finalize();
			exit(1);
		}
		fd_locations = fileno(fp);
		if (fcntl(fd_locations, F_SETLKW, &lock) < 0) {
        	close(fd_locations);
			MPI_Finalize();
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
		if (fcntl(fd, F_SETLK, &lock) < 0) {
			std::cerr << "reader " << rank << " failed to unlock the file" << std::endl;
		}
		fclose(fp);
	}
	//check if the file is local or remote
	if (strcmp(files_location[path_to_check], node_name) == 0) {
			handle_local_read(pid, fd, count);
	}
	else {
			handle_remote_read(pid, fd, count, rank);
	}

	free(metadata);
	return nullptr;
}


void handle_read(char* str, int rank) {
	#ifdef CAPIOLOG
	std::cout << "handle read" << std::endl;
	#endif
	std::string request;
	int pid, fd;
	long int count;
	std::istringstream stream(str);
	stream >> request >> pid >> fd >> count;
	if (processes_files[pid][fd].second == 0 && files_location.find(processes_files_metadata[pid][fd]) == files_location.end()) {
		check_remote_file("files_location.txt", rank, processes_files_metadata[pid][fd]);
		if (files_location.find(processes_files_metadata[pid][fd]) == files_location.end()) {
			std::string path = processes_files_metadata[pid][fd];
			//launch a thread that checks when the file is created
			pthread_t t;
			struct wait_for_file_metadata* metadata = (struct wait_for_file_metadata*)  malloc(sizeof(wait_for_file_metadata));
			metadata->pid = pid;
			metadata->fd = fd;
			int res = pthread_create(&t, NULL, wait_for_file, (void*) metadata);
			if (res != 0) {
				std::cerr << "error creation of capio server thread" << std::endl;
				MPI_Finalize();
				exit(1);
			}

			return;
		}
	}
	if (strcmp(files_location[processes_files_metadata[pid][fd]], node_name) == 0) {
		handle_local_read(pid, fd, count);
	}
	else {
		handle_remote_read(pid, fd, count, rank);
	}
	
}

void handle_close(char* str, char* p) {
	#ifdef CAPIOLOG
	std::cout << "handle close" << std::endl;
	#endif
	strtol(str + 5, &p, 10);;
	int fd = strtol(p, &p, 10);
	if (close(fd) == -1) {
		std::cerr << "capio server, error: impossible close the file with fd = " << fd << std::endl;
		MPI_Finalize();
		exit(1);
	}
}

void handle_remote_read(char* str, char* p, int rank) {
	long int bytes_received, offset;
	char path_c[30];
	sscanf(str, "ream %s %li %li", path_c, &bytes_received, &offset);
	std::string path(path_c);
	int pid, fd;
	long int count; //TODO: diff between count and bytes_received

	std::list<std::tuple<int, int, long int>>& list_remote_reads = my_remote_pending_reads[path];
	auto it = list_remote_reads.begin();
	std::list<std::tuple<int, int, long int>>::iterator prev_it;
	while (it != list_remote_reads.end()) {
		pid = std::get<0>(*it);
		fd = std::get<1>(*it);
		count = std::get<2>(*it);
		long int fd_offset = processes_files[pid][fd].second;
		if (fd_offset + count <= offset + bytes_received) {
			//this part is equals to the local read (TODO: function)
			#ifdef MYDEBUG
			int* tmp = (int*) malloc(count);
			memcpy(tmp, processes_files[pid][fd].first + processes_files[pid][fd].second, count); 
			for (int i = 0; i < count / sizeof(int); ++i) {
				std::cout << "server remote read tmp[i] " << tmp[i] << std::endl;
			}
			free(tmp);
			#endif
			response_buffers[pid]->write(files_metadata[path].second);
			processes_files[pid][fd].second += count;
			//TODO: check if there is data that can be read in the local memory file
			if (it == list_remote_reads.begin()) {
				list_remote_reads.erase(it);
				it = list_remote_reads.begin();
			}
			else {
				it = std::next(prev_it);
			}
		}
		else {
			prev_it = it;
			++it;
		}
	}
}



void read_next_msg(int rank) {
	char str[4096];
	std::fill(str, str + 4096, 0);
	bool is_open = false;
	buf_requests->read(str);
	char* p = str;
	is_open = strncmp(str, "open", 4) == 0;
	#ifdef CAPIOLOG
	std::cout << "next msg " << str << std::endl;
	#endif
	if (is_open) {
		handle_open(str, p, rank);
	}
	else {
		bool is_write = strncmp(str, "writ", 4) == 0;
		if (is_write) {
			/*pthread_t t;
			struct handle_write_metadata* rr_metadata = (struct handle_write_metadata*) malloc(sizeof(struct handle_write_metadata));
			rr_metadata->path = path_c;
			rr_metadata->rank = rank;
			rr_metadata->sem = ;
			int res = pthread_create(&t, NULL, wait_for_data, (void*) rr_metadata);
			if (res != 0) {
				std::cerr << "error creation of capio server thread" << std::endl;
				MPI_Finalize();
				exit(1);
			}*/
			handle_write(str, rank);
		}
		else {
		bool is_read = strncmp(str, "read", 4) == 0;
			if (is_read) {
				handle_read(str, rank);
			}
			else {
				bool is_close = strncmp(str, "clos", 4) == 0;
				if (is_close) { //TODO: more cleaning
					handle_close(str, p);
				}
				else {
					bool is_remote_read = strncmp(str, "ream", 4) == 0;
					if (is_remote_read) {
						handle_remote_read(str, p, rank);
					}
					else {
						std::cerr << "error msg read" << std::endl;
						MPI_Finalize();
						exit(1);
					}
				}
			}
		}
	}
	return;
}

void handshake_servers(int rank, int size) {
	char* buf;	
	for (int i = 0; i < size; i += 1) {
		if (i != rank) {
			MPI_Send(node_name, strlen(node_name), MPI_CHAR, i, 0, MPI_COMM_WORLD); //TODO: possible deadlock
			buf = (char*) malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));//TODO: free
			MPI_Recv(buf, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			nodes_helper_rank[buf] = i;
		}
	}
}
void* capio_server(void* pthread_arg) {
	int rank, size;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	catch_sigterm();
	handshake_servers(rank, size);
	buf_requests = new Circular_buffer<char>("circular_buffer", 256L * 1024 * 1024, sizeof(char) * 64);
	sem_post(&internal_server_sem);
	while(true) {
		read_next_msg(rank);

		//respond();
	}
	return nullptr; //pthreads always needs a return value
}

struct remote_read_metadata {
	char* path;
	long int offset;
	int dest;
	long int nbytes;
	sem_t* sem;
};

//TODO: refactor offset_str and offset

void serve_remote_read(const char* path_c, const char* offset_str, int dest, long int offset, long int nbytes) {
	char* buf_send;
	const char* s1 = "sending";
	const size_t len1 = strlen(s1);
	const size_t len2 = strlen(path_c);
	const size_t len3 = strlen(offset_str);
	buf_send = (char*) malloc((len1 + len2 + len3 + 3) * sizeof(char));//TODO:add malloc check
	sprintf(buf_send, "%s %s %s", s1, path_c, offset_str);
	//send warning
	MPI_Send(buf_send, strlen(buf_send) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
	free(buf_send);
	//send data
	void* file_shm = get_shm(path_c);
	#ifdef MYDEBUG
	int* tmp = (int*) malloc(*size_shm);
	std::cout << "helper sending " << *size_shm << " bytes" << std::endl;
	memcpy(tmp, file_shm, *size_shm); 
	for (int i = 0; i < *size_shm / sizeof(int); ++i) {
		std::cout << "helper sending tmp[i] " << tmp[i] << std::endl;
	}
	free(tmp);
	#endif
	MPI_Send(((char*)file_shm) + offset, nbytes, MPI_BYTE, dest, 0, MPI_COMM_WORLD); 
}

void* wait_for_data(void* pthread_arg) {
	struct remote_read_metadata* rr_metadata = (struct remote_read_metadata*) pthread_arg;
	const char* path = rr_metadata->path;
	long int offset = rr_metadata->offset;
	int dest = rr_metadata->dest;
	long int nbytes = rr_metadata->nbytes;
	const char * offset_str = std::to_string(offset).c_str();
	sem_wait(rr_metadata->sem);
	serve_remote_read(path, offset_str, dest, offset, nbytes);
	free(rr_metadata->sem);
	free(rr_metadata->path);
	free(rr_metadata);
	return nullptr;
}


bool data_avaiable(const char* path_c, long int offset, long int nbytes_requested) {
	long int file_size = *files_metadata[path_c].second;
	return offset + nbytes_requested <= file_size;
}

void lightweight_MPI_Recv(char* buf_recv) {
	MPI_Request request;
	int received = 0;
	MPI_Irecv(buf_recv, 2048, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &request); //receive from server
	struct timespec sleepTime;
    struct timespec returnTime;
    sleepTime.tv_sec = 0;
    sleepTime.tv_nsec = 200000;

	while (!received) {
		MPI_Test(&request, &received, MPI_STATUS_IGNORE);
		nanosleep(&sleepTime, &returnTime);
	}
}


void* capio_helper(void* pthread_arg) {
	char buf_recv[2048];
	MPI_Status status;
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	sem_wait(&internal_server_sem);
	while(true) {
		lightweight_MPI_Recv(buf_recv); //receive from server
		bool remote_request_to_read = strncmp(buf_recv, "read", 4) == 0;
		if (remote_request_to_read) {
		    // schema msg received: "read path dest offset nbytes"
			char* path_c = (char*) malloc(sizeof(char) * 512);
			int i = 5;
			int j = 0;
			while (buf_recv[i] != ' ') {
				path_c[j] = buf_recv[i];
				++i;
				++j;
			}
			path_c[j] = '\0';
			char dest_str[128];
			j = 0;
			++i;
			while (buf_recv[i] != ' ') {
				dest_str[j] = buf_recv[i];
				++i;
				++j;
			}
			dest_str[j] = '\0';
			int dest = std::atoi(dest_str);
			char offset_str[128];
			j = 0;
			++i;
			while (buf_recv[i] != ' ') {
				offset_str[j] = buf_recv[i];
				++i;
				++j;
			}
			offset_str[j] = '\0';
			long int offset = std::atoi(offset_str);
			char nbytes_str[128];
			j = 0;
			++i;
			while (buf_recv[i] != '\0') {
				nbytes_str[j] = buf_recv[i];
				++i;
				++j;
			}
			nbytes_str[j] = '\0';
			long int nbytes = std::atoi(nbytes_str);
			//check if the data is avaiable
			if (data_avaiable(path_c, offset, nbytes)) {
				serve_remote_read(path_c, offset_str, dest, offset, nbytes);
			}
			else {
				pthread_t t;
				struct remote_read_metadata* rr_metadata = (struct remote_read_metadata*) malloc(sizeof(struct remote_read_metadata));
				rr_metadata->path = path_c;
				rr_metadata->offset = offset;
				rr_metadata->dest = dest;
				rr_metadata->nbytes = nbytes;
				rr_metadata->sem = (sem_t*) malloc(sizeof(sem_t));
				int res = sem_init(rr_metadata->sem, 0, 0);
				if (res !=0) {
					std::cerr << __FILE__ << ":" << __LINE__ << " - " << std::flush;
					perror("sem_init failed"); 
					exit(1);
				}
				res = pthread_create(&t, NULL, wait_for_data, (void*) rr_metadata);
				if (res != 0) {
					std::cerr << "error creation of capio server thread" << std::endl;
					MPI_Finalize();
					exit(1);
				}
				clients_remote_pending_reads[path_c].push_back(std::make_tuple(offset, nbytes, rr_metadata->sem));
			}
		}
		else if(strncmp(buf_recv, "sending", 7) == 0) { //receiving a file
			int pos = std::string((buf_recv + 8)).find(" ");
			std::string path(buf_recv + 8);
			path = path.substr(0, pos);
			void* file_shm =  get_shm(path);
			int bytes_received;
			int source = status.MPI_SOURCE;
			int offset = std::atoi(buf_recv + pos + 9);
			MPI_Recv((char*)file_shm + offset, 1024L * 1024 * 1024, MPI_BYTE, source, 0, MPI_COMM_WORLD, &status);//TODO; 4096 should be a parameter
			MPI_Get_count(&status, MPI_CHAR, &bytes_received); //why in recv MPI_BYTE while ehre MPI_CHAR?
			bytes_received *= sizeof(char);
			#ifdef MYDEBUG
			int* tmp = (int*) malloc(bytes_received);
			memcpy(tmp, file_shm, bytes_received); 
			for (int i = 0; i < bytes_received / sizeof(int); ++i) {
				std::cout << "helper receiving tmp[i] " << tmp[i] << std::endl;
			}	
			free(tmp);
			#endif
			std::string msg = "ream " + path + + " " + std::to_string(bytes_received) + " " + std::to_string(offset);
			const char* c_str = msg.c_str();
			buf_requests->write(c_str);

		}
		else {
			std::cerr << "helper error receiving message" << std::endl;
		}
	}
	return nullptr; //pthreads always needs a return value
}



int main(int argc, char** argv) {
	int rank, len, provided;
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if(provided != MPI_THREAD_MULTIPLE)
    {
        std::cerr << "The threading support level is lesser than that demanded" << std::endl;
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
	MPI_Get_processor_name(node_name, &len);
	pthread_t server_thread, helper_thread;
	int res = sem_init(&internal_server_sem,0,0);
	if (res !=0) {
		std::cerr << __FILE__ << ":" << __LINE__ << " - " << std::flush;
		perror("sem_init failed"); exit(res);
	}
	res = pthread_create(&server_thread, NULL, capio_server, &rank);
	if (res != 0) {
		std::cerr << "error creation of capio server thread" << std::endl;
		MPI_Finalize();
		return 1;
	}
	res = pthread_create(&helper_thread, NULL, capio_helper, &rank);
	if (res != 0) {
		std::cerr << "error creation of helper server thread" << std::endl;
		MPI_Finalize();
		return 1;
	}
    void* status;
    int t = pthread_join(server_thread, &status);
    if (t != 0) {
    	std::cerr << "Error in thread join: " << t << std::endl;
    }
    t = pthread_join(helper_thread, &status);
    if (t != 0) {
    	std::cerr << "Error in thread join: " << t << std::endl;
    }
	res = sem_destroy(&internal_server_sem);
	if (res !=0) {
		std::cerr << __FILE__ << ":" << __LINE__ << " - " << std::flush;
		perror("sem_destroy failed"); exit(res);
	}
	MPI_Finalize();
	return 0;
}
