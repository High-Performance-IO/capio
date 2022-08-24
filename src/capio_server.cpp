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

#include <mpi.h>

#include "capio_file.hpp"
#include "utils/common.hpp"

const long int max_shm_size = 1024L * 1024 * 1024 * 16;

// maximum size of shm for each file
const long int max_shm_size_file = 1024L * 1024 * 1024 * 8;

// initial size for each file (can be overwritten by the user)
const size_t file_initial_size = 1024L * 1024 * 1024 * 2;

bool shm_full = false;
long int total_bytes_shm = 0;

MPI_Request req;

// pid -> fd ->(file_shm, index)
std::unordered_map<int, std::unordered_map<int, std::tuple<void*, off64_t*>>> processes_files;

// pid -> fd -> pathname
std::unordered_map<int, std::unordered_map<int, std::string>> processes_files_metadata;

// pid -> (response shared buffer, index)
std::unordered_map<int, Circular_buffer<off_t>*> response_buffers;

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

/* pathname -> (file_shm, file_size, mapped_shm_size, first_write, capio_file)
 * The mapped shm size isn't the the size of the file shm
 * but it's the mapped shm size in the virtual adress space
 * of the server process. The effective size can ge greater
 * in a given moment.
 */
std::unordered_map<std::string, std::tuple<void*, off64_t*, off64_t, bool, Capio_file>> files_metadata;

/*
 * pid -> pathname -> bool
 */

std::unordered_map<int, std::unordered_map<std::string, bool>> writers;

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

std::unordered_map<std::string, std::vector<std::tuple<int, int, off64_t>>>  pending_reads;

/*
 *
 * It contains all the reads requested to the remote nodes that are not yet satisfied 
 * path -> [(pid, fd, numbytes), ...]
 *
 */

std::unordered_map<std::string, std::list<std::tuple<int, int, off64_t>>>  my_remote_pending_reads;

/*
 *
 * It contains all the read requested by other nodes for which the data is not yet avaiable 
 * path -> [(offset, numbytes, sem_pointer), ...]
 *
 */

std::unordered_map<std::string, std::list<std::tuple<size_t, size_t, sem_t*>>> clients_remote_pending_reads;

// it contains the file saved on disk
std::unordered_set<std::string> on_disk;


//name of the node

char node_name[MPI_MAX_PROCESSOR_NAME];

Circular_buffer<char>* buf_requests; 
std::unordered_map<int, sem_t*> sems_response;
std::unordered_map<int, sem_t*> sems_write;

sem_t internal_server_sem;
sem_t remote_read_sem;
sem_t handle_remote_read_sem;
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

void write_file_location(const std::string& file_name, int rank, std::string path_to_write) {
        #ifdef CAPIOLOG
        std::cout << "write file location before" << std::endl;
        #endif
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
        #ifdef CAPIOLOG
        std::cout << "write file location after" << std::endl;
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
		if (strcmp(path, path_to_check_cstr) == 0) {
			files_location[path_to_check] = (char*) malloc(sizeof(node_str) + 1); //TODO:free the memory
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
	void* buf = std::get<0>(processes_files[pid][fd]);
	std::string path = processes_files_metadata[pid][fd];
	long int file_size = *std::get<1>(files_metadata[path]);	
	long int num_bytes_written, k = 0;
	while (k < file_size) {
    	num_bytes_written = write(fd, ((char*) buf) + k, (file_size - k));
    	k += num_bytes_written;
    }
}

void init_process(int pid) {
	if (sems_response.find(pid) == sems_response.end()) {
		sems_response[pid] = sem_open(("sem_response_read" + std::to_string(pid)).c_str(), O_RDWR);
		if (sems_response[pid] == SEM_FAILED) {
			err_exit("error creating sem_response_read" + std::to_string(pid));  	
		}
		sems_write[pid] = sem_open(("sem_write" + std::to_string(pid)).c_str(), O_RDWR);
		if (sems_write[pid] == SEM_FAILED) {
			err_exit("error creating sem_write" + std::to_string(pid));
		}
		Circular_buffer<off_t>* cb = new Circular_buffer<off_t>("buf_response" + std::to_string(pid), 256L * 1024 * 1024, sizeof(off_t));
		response_buffers.insert({pid, cb});
		caching_info[pid].first = (int*) get_shm("caching_info" + std::to_string(pid));
		caching_info[pid].second = (int*) get_shm("caching_info_size" + std::to_string(pid));
	}

}
void create_file(std::string path, void* p_shm, bool is_dir) {
		off64_t* p_shm_size = create_shm_off64_t(path + "_size");	
		files_metadata[path] = std::make_tuple(p_shm, p_shm_size, file_initial_size, true, Capio_file(is_dir));

}

//TODO: function too long

void handle_open(char* str, char* p, int rank) {
	#ifdef CAPIOLOG
	std::cout << "handle open" << std::endl;
	#endif
	int pid;
	pid = strtol(str + 5, &p, 10);
	init_process(pid);
	int fd = strtol(p + 1, &p, 10);
	std::string path(p + 1);
	void* p_shm;
	int index = *caching_info[pid].second;
	caching_info[pid].first[index] = fd;
	if (on_disk.find(path) == on_disk.end()) {
		p_shm = create_shm(path, 1024L * 1024 * 1024* 2);
		caching_info[pid].first[index + 1] = 0;
	}
	else {
		p_shm = nullptr;
		caching_info[pid].first[index + 1] = 1;
	}
		//TODO: check the size that the user wrote in the configuration file
	off64_t* p_offset = create_shm_off64_t("offset_" + std::to_string(pid) + "_" + std::to_string(fd));
	processes_files[pid][fd] = std::make_tuple(p_shm, p_offset);//TODO: what happens if a process open the same file twice?
	*caching_info[pid].second += 2;
	if (files_metadata.find(path) == files_metadata.end()) {
		create_file(path, p_shm, false);
	}
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	++c_file.n_opens;
	std::cout << "capio open n links " << c_file.n_links << " n opens " << c_file.n_opens << std::endl;;
	std::cout << "path " << path << std::endl;
	processes_files_metadata[pid][fd] = path;
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

void handle_pending_read(int pid, int fd, long int process_offset, long int count) {
	
	std::string path = processes_files_metadata[pid][fd];
	Capio_file & c_file = std::get<4>(files_metadata[path]);
	off64_t end_of_sector = c_file.get_sector_end(process_offset);
	response_buffers[pid]->write(&end_of_sector);
	//*processes_files[pid][fd].second += count;
	//TODO: check if the file was moved to the disk

}

struct handle_write_metadata{
	char str[64];
	long int rank;
};

void handle_write(const char* str, int rank) {
        //check if another process is waiting for this data
        std::string request;
        int pid, fd;
		off_t base_offset;
        off64_t count;
        off64_t data_size;
        std::istringstream stream(str);
        stream >> request >> pid >> fd >> base_offset >> count;
		data_size = base_offset + count;
        std::string path = processes_files_metadata[pid][fd];
		writers[pid][path] = true;
		Capio_file& c_file = std::get<4>(files_metadata[path]);
		std::cout << "insert sector " << base_offset << ", " << data_size << std::endl;
		c_file.insert_sector(base_offset, data_size);
		c_file.print();
        #ifdef CAPIOLOG
        std::cout << "handle write pid fd " << pid << " " << fd << std::endl;
        #endif
        if (std::get<3>(files_metadata[path])) {
			std::get<3>(files_metadata[path]) = false;
                write_file_location("files_location.txt", rank, path);
        }
        *std::get<1>(files_metadata[path]) = data_size; 
		off64_t file_shm_size = std::get<2>(files_metadata[path]);
		if (data_size > file_shm_size) {

        #ifdef CAPIOLOG
        std::cout << "handle write data_size > file_shm_size" << std::endl;
        #endif
			//remap
			size_t new_size;
			if (data_size > file_shm_size * 2)
				new_size = data_size;
			else
				new_size = file_shm_size * 2;

			void* p = mremap(std::get<0>(files_metadata[path]), file_shm_size, new_size, MREMAP_MAYMOVE);
			if (p == MAP_FAILED)
				err_exit("mremap " + path);
			std::get<0>(files_metadata[path]) = p;
			std::get<2>(files_metadata[path]) = new_size;
		}
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
        #ifdef CAPIOLOG
        std::cout << "There were pending reads for" << path << std::endl;
        #endif
                auto& pending_reads_this_file = it->second;
				auto it_vec = pending_reads_this_file.begin();
                while (it_vec != pending_reads_this_file.end()) {
                        auto tuple = *it_vec;
                        int pending_pid = std::get<0>(tuple);
                        int fd = std::get<1>(tuple);
                        size_t process_offset = *std::get<1>(processes_files[pending_pid][fd]);
                        size_t count = std::get<2>(tuple);
                        size_t file_size = *std::get<1>(files_metadata[path]);
        #ifdef CAPIOLOG
        std::cout << "pending read offset " << process_offset << " count " << count << " file_size " << file_size << std::endl;
        #endif
                        if (process_offset + count <= file_size) {
        #ifdef CAPIOLOG
        std::cout << "handling this pending read"<< std::endl;
        #endif
                                handle_pending_read(pending_pid, fd, process_offset, count);
                        it_vec = pending_reads_this_file.erase(it_vec);
                        }
						else
							++it_vec;
                }
        }
        sem_post(sems_write[pid]);
        auto it_client = clients_remote_pending_reads.find(path);
		std::list<std::tuple<size_t, size_t, sem_t*>>::iterator it_list, prev_it_list;
        if (it_client !=  clients_remote_pending_reads.end()) {

        #ifdef CAPIOLOG
        std::cout << "handle write serving remote pending reads" << std::endl;
        #endif
			it_list = it_client->second.begin();
                while (it_list != it_client->second.end()) {
                        off64_t offset = std::get<0>(*it_list);
                        off64_t nbytes = std::get<1>(*it_list);
                        sem_t* sem = std::get<2>(*it_list);
        #ifdef CAPIOLOG
        std::cout << "handle write serving remote pending reads inside the loop" << std::endl;
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
}


/*
 * Multithread function
 */

void handle_local_read(int pid, int fd, off64_t count) {
		#ifdef CAPIOLOG
		std::cout << "handle local read" << std::endl;
		#endif
		sem_wait(&handle_local_read_sem);
		std::string path = processes_files_metadata[pid][fd];
		Capio_file & c_file = std::get<4>(files_metadata[path]);
		off64_t process_offset = *std::get<1>(processes_files[pid][fd]);
		bool writer = writers[pid][path];
		off64_t end_of_sector = c_file.get_sector_end(process_offset);
		std::cout << "process offset " << process_offset << std::endl;
		std::cout << "count " << count << std::endl;
		std::cout << "end of sector" << end_of_sector << std::endl;
		c_file.print();
		off64_t end_of_read = process_offset + count;
		if (end_of_read > end_of_sector) {
		#ifdef CAPIOLOG
		std::cout << "Am I a writer? " << writer << std::endl;
		std::cout << "Is the file completed? " << c_file.complete << std::endl;
		#endif
			if (!writer && !c_file.complete) {
				#ifdef CAPIOLOG
				std::cout << "add pending reads" << std::endl;
				#endif
				pending_reads[path].push_back(std::make_tuple(pid, fd, count));
			}
			else {
				off64_t file_size = c_file.get_file_size();
				if (file_size >= end_of_read)
					response_buffers[pid]->write(&end_of_read);
				else
					response_buffers[pid]->write(&file_size);
			}

		}
		else
			response_buffers[pid]->write(&end_of_sector);
		#ifdef CAPIOLOG
		std::cout << "process offset " << process_offset << std::endl;
		#endif
		sem_post(&handle_local_read_sem);
		//*processes_files[pid][fd].second += count;
		//TODO: check if the file was moved to the disk
}

/*
 * Multithread function
 *
 */

void handle_remote_read(int pid, int fd, off64_t count, int rank) {
		#ifdef CAPIOLOG
		std::cout << "handle remote read before sem_wait" << std::endl;
		#endif
		sem_wait(&handle_remote_read_sem);
		const char* msg;
		std::string str_msg;
		int dest = nodes_helper_rank[files_location[processes_files_metadata[pid][fd]]];
		size_t offset = *std::get<1>(processes_files[pid][fd]);
		str_msg = "read " + processes_files_metadata[pid][fd] + " " + std::to_string(rank) + " " + std::to_string(offset) + " " + std::to_string(count); 
		msg = str_msg.c_str();
		#ifdef CAPIOLOG
		std::cout << "handle remote read" << std::endl;
		std::cout << "msg sent " << msg << std::endl;
		std::cout << processes_files_metadata[pid][fd] << std::endl;
		std::cout << "dest " << dest << std::endl;
		std::cout << "rank" << rank << std::endl;
		#endif
		MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
		my_remote_pending_reads[processes_files_metadata[pid][fd]].push_back(std::make_tuple(pid, fd, count));
		sem_post(&handle_remote_read_sem);
}

struct wait_for_file_metadata{
	int pid;
	int fd;
	off64_t count;
};

void* wait_for_file(void* pthread_arg) {
	struct wait_for_file_metadata* metadata = (struct wait_for_file_metadata*) pthread_arg;
	int pid = metadata->pid;
	int fd = metadata-> fd;
	off64_t count = metadata->count;
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

	
	#ifdef CAPIOLOG
	std::cout << "wait for file before" << std::endl;
	#endif
	
	std::string path_to_check(processes_files_metadata[pid][fd]);
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
			std::cerr << "error fopen files_location.txt" << std::endl;
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
	std::cout << "handle read str" << str << std::endl;
	#endif
	std::string request;
	int pid, fd;
	off64_t count;
	std::istringstream stream(str);
	stream >> request >> pid >> fd >> count;
	if (*std::get<1>(processes_files[pid][fd]) == 0 && files_location.find(processes_files_metadata[pid][fd]) == files_location.end()) {
		check_remote_file("files_location.txt", rank, processes_files_metadata[pid][fd]);
		if (files_location.find(processes_files_metadata[pid][fd]) == files_location.end()) {
			std::string path = processes_files_metadata[pid][fd];
			//launch a thread that checks when the file is created
			pthread_t t;
			struct wait_for_file_metadata* metadata = (struct wait_for_file_metadata*)  malloc(sizeof(wait_for_file_metadata));
			metadata->pid = pid;
			metadata->fd = fd;
			metadata->count = count;
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


void delete_file(std::string path) {
	#ifdef CAPIOLOG
	std::cout << "deleting file " << path << std::endl;
	#endif	
	auto it_metadata = files_metadata.find(path);
	shm_unlink(path.c_str());
	shm_unlink((path + "_size").c_str());
	files_metadata.erase(path);

}

void handle_close(int pid, int fd) {
	std::string path = processes_files_metadata[pid][fd];
	std::cout << "path name " << path << std::endl;
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	--c_file.n_opens;
	std::cout << "capio close n links " << c_file.n_links << " n opens " << c_file.n_opens << std::endl;;
	if (c_file.n_opens == 0 && c_file.n_links <= 0)
		delete_file(path);
	shm_unlink(("offset_" + std::to_string(pid) +  "_" + std::to_string(fd)).c_str());
	processes_files[pid].erase(fd);
	processes_files_metadata[pid].erase(fd);
}

void handle_close(char* str, char* p) {
	int pid, fd;
	sscanf(str, "clos %d %d", &pid, &fd);
	#ifdef CAPIOLOG
	std::cout << "handle close " << pid << " " << fd << std::endl;
	#endif
	handle_close(pid, fd);
}

void handle_remote_read(char* str, char* p, int rank) {
	size_t bytes_received, offset;
	char path_c[30];
	sscanf(str, "ream %s %zu %zu", path_c, &bytes_received, &offset);
	#ifdef CAPIOLOG
		std::cout << "serving the remote read: " << str << std::endl;
	#endif
	*std::get<1>(files_metadata[path_c]) += bytes_received;
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
		size_t fd_offset = *std::get<1>(processes_files[pid][fd]);
		if (fd_offset + count <= offset + bytes_received) {
		#ifdef CAPIOLOG
			std::cout << "handling others remote reads fd_offset " << fd_offset << " count " << count << " offset " << offset << " bytes received " << bytes_received << std::endl;
		#endif
			//this part is equals to the local read (TODO: function)
			response_buffers[pid]->write(std::get<1>(files_metadata[path]));
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

void handle_lseek(char* str) {
	int pid, fd;	
	size_t offset;
	sscanf(str, "seek %d %d %zu", &pid, &fd, &offset);
	std::string path = processes_files_metadata[pid][fd];
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	off64_t sector_end = c_file.get_sector_end(offset);
	response_buffers[pid]->write(&sector_end);
}

void handle_sdat(char* str) {
	int pid, fd;	
	size_t offset;
	sscanf(str, "sdat %d %d %zu", &pid, &fd, &offset);
	std::string path = processes_files_metadata[pid][fd];
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	off64_t res = c_file.seek_data(offset);
	response_buffers[pid]->write(&res);
}

void handle_shol(char* str) {
	int pid, fd;	
	size_t offset;
	sscanf(str, "shol %d %d %zu", &pid, &fd, &offset);
	std::string path = processes_files_metadata[pid][fd];
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	off64_t res = c_file.seek_hole(offset);
	response_buffers[pid]->write(&res);
}

void handle_seek_end(char* str) {
	int pid, fd;	
	sscanf(str, "send %d %d", &pid, &fd);
	std::string path = processes_files_metadata[pid][fd];
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	off64_t res = c_file.get_file_size();
	response_buffers[pid]->write(&res);
}

void close_all_files(int pid) {
	auto it_process_files = processes_files.find(pid);
	if (it_process_files != processes_files.end()) {
		auto process_files = it_process_files->second;
		for (auto it : process_files) {
			handle_close(pid, it.first);
		}
	}
}

void handle_exig(char* str) {
	int pid;
	sscanf(str, "exig %d", &pid);
	#ifdef CAPIOLOG
	std::cout << "handle exit group " << std::endl;
	#endif	
   // std::unordered_map<int, std::unordered_map<std::string, bool>> writers;
   auto files = writers[pid];
   for (auto& pair : files) {
   	if (pair.second) {
		std::string path = pair.first;	
		std::get<4>(files_metadata[path]).complete = true;
        sem_wait(sems_write[pid]);
		auto it = pending_reads.find(path);
        if (it != pending_reads.end()) {
	#ifdef CAPIOLOG
	std::cout << "handle pending read file " << path << std::endl;
	#endif	
                auto& pending_reads_this_file = it->second;
				auto it_vec = pending_reads_this_file.begin();
                while (it_vec != pending_reads_this_file.end()) {
                        auto tuple = *it_vec;
                        int pending_pid = std::get<0>(tuple);
                        int fd = std::get<1>(tuple);
                        size_t process_offset = *std::get<1>(processes_files[pending_pid][fd]);
                        size_t count = std::get<2>(tuple);
                        size_t file_size = *std::get<1>(files_metadata[path]);
						#ifdef CAPIOLOG
						std::cout << "pending read pid fd offset count " << pid << " " << fd << " " << process_offset <<" "<< count << std::endl;
						#endif	
                        handle_pending_read(pending_pid, fd, process_offset, count);
                        it_vec = pending_reads_this_file.erase(it_vec);
                }
        }
        sem_post(sems_write[pid]);
	}
   }
   close_all_files(pid);
}

void handle_stat(const char* str) {
	char path[2048];
	int pid;
	sscanf(str, "stat %d %s", &pid, path);
	auto it = files_metadata.find(path);
	off64_t file_size;
	init_process(pid);
	if (it == files_metadata.end()) {
		file_size = -1;	
		response_buffers[pid]->write(&file_size);
	}
	else {
		Capio_file& c_file = std::get<4>(it->second);
		file_size = c_file.get_file_size();
		off64_t is_dir;
		if (c_file.is_dir())
			is_dir = 0;
		else
			is_dir = 1;
		response_buffers[pid]->write(&file_size);
		response_buffers[pid]->write(&is_dir);
	}
	#ifdef CAPIOLOG
		std::cout << "file size stat : " << file_size << std::endl;
	#endif
}

void handle_fstat(const char* str) {
	int pid, fd;
	sscanf(str, "fsta %d %d", &pid, &fd);
	std::string path = processes_files_metadata[pid][fd];
	std::cout << "path " << path << std::endl;
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	off64_t file_size = c_file.get_file_size();
	response_buffers[pid]->write(&file_size);
	off64_t is_dir;
	if (c_file.is_dir())
		is_dir = 0;
	else
		is_dir = 1;
	response_buffers[pid]->write(&is_dir);
}

void handle_access(const char* str) {
	int pid;
	char path[PATH_MAX];
	#ifdef CAPIOLOG
		std::cout << "handle access: " << str << std::endl;
	#endif
	sscanf(str, "accs %d %s", &pid, path);
	off64_t res;
	auto it = files_location.find(path);
	if (it == files_location.end())
		res = -1;
	else 
		res = 0;
	response_buffers[pid]->write(&res);
}


void handle_unlink(const char* str) {
	char path[PATH_MAX];
	off64_t res;
	int pid;
	#ifdef CAPIOLOG
		std::cout << "handle unlink: " << str << std::endl;
	#endif
	sscanf(str, "unlk %d %s", &pid, path);
	auto it = files_metadata.find(path);
	if (it != files_metadata.end()) { //TODO: it works only in the local case
		Capio_file& c_file = std::get<4>(it->second);
		--c_file.n_links;
		std::cout << "capio unlink n links " << c_file.n_links << " n opens " << c_file.n_opens;
		if (c_file.n_opens == 0 && c_file.n_links <= 0) {
			delete_file(path);
		}
		res = 0;
	}
	else
		res = -1;
	response_buffers[pid]->write(&res);
}

std::unordered_set<std::string> get_paths_opened_files(pid_t pid) {
	std::unordered_set<std::string> set;
	for (auto& it : processes_files_metadata[pid])
		set.insert(it.second);
	return set;
}

//TODO: caching info
void handle_clone(const char* str) {
	pid_t ppid, new_pid;
	sscanf(str, "clon %d %d", &ppid, &new_pid);
	init_process(new_pid);
	processes_files[new_pid] = processes_files[ppid];
	processes_files_metadata[new_pid] = processes_files_metadata[ppid];
	writers[new_pid] = writers[ppid];
	for (auto &p : writers[new_pid]) {
		p.second = false;
	}
	std::unordered_set<std::string> parent_files = get_paths_opened_files(ppid);
	for(std::string path : parent_files) {
		Capio_file& c_file = std::get<4>(files_metadata[path]);
		++c_file.n_opens;
	}
}

void handle_mkdir(const char* str) {
	pid_t pid;
	char pathname[PATH_MAX];
	sscanf(str, "mkdi %d %s", &pid, pathname);
	init_process(pid);
	off64_t res;
	#ifdef CAPIOLOG
	std::cout << "handle mkdir" << std::endl;
	#endif
	if (files_metadata.find(pathname) == files_metadata.end()) {
		void* p_shm = create_shm(pathname, 1024L * 1024 * 1024* 2);
		create_file(pathname, p_shm, true);	
		res = 0;
	}
	else {
		res = 1;
	}
	response_buffers[pid]->write(&res);

}

void handle_dup(const char* str) {
	pid_t pid;
	int old_fd, new_fd;
	sscanf(str, "dupp %d %d %d", &pid, &old_fd, &new_fd);
	processes_files[pid][new_fd] = processes_files[pid][old_fd];
	std:: string path = processes_files_metadata[pid][old_fd];
	processes_files_metadata[pid][new_fd] = path;
	Capio_file& c_file = std::get<4>(files_metadata[path]);
	++c_file.n_opens;
}

void read_next_msg(int rank) {
	char str[4096];
	std::fill(str, str + 4096, 0);
	buf_requests->read(str);
	char* p = str;
	#ifdef CAPIOLOG
	std::cout << "next msg " << str << std::endl;
	#endif
	if (strncmp(str, "open", 4) == 0)
		handle_open(str, p, rank);
	else if (strncmp(str, "writ", 4) == 0)
		handle_write(str, rank);
	else if (strncmp(str, "read", 4) == 0)
		handle_read(str, rank);
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
		handle_stat(str);
	else if (strncmp(str, "fsta", 4) == 0)
		handle_fstat(str);
	else if (strncmp(str, "accs", 4) == 0)
		handle_access(str);
	else if (strncmp(str, "unlk", 4) == 0)
		handle_unlink(str);
	else if (strncmp(str, "clon", 4) == 0)
		handle_clone(str);
	else if (strncmp(str, "mkdi", 4) == 0)
		handle_mkdir(str);
	else if (strncmp(str, "dupp", 4) == 0)
		handle_dup(str);
	else {
		std::cerr << "error msg read" << std::endl;
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
		std::cerr << "impossible close the file files_location" << std::endl;
	}
}

void handshake_servers(int rank, int size) {
	char* buf;	
	if (rank == 0) {
		clean_files_location();
	}
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
	buf_requests = new Circular_buffer<char>("circular_buffer", 1024 * 1024, sizeof(char) * 600);
	sem_post(&internal_server_sem);
	while(true) {
		read_next_msg(rank);
		#ifdef CAPIOLOG
		std::cout << "after next msg " << std::endl;
		#endif

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

void send_file(char* shm, long int nbytes, int dest) {
	long int num_elements_to_send = 0;
	for (long int k = 0; k < nbytes; k += num_elements_to_send) {
			if (nbytes - num_elements_to_send > 1024L * 1024 * 1024)
				num_elements_to_send = 1024L * 1024 * 1024;
			else
				num_elements_to_send = nbytes - num_elements_to_send;
			MPI_Isend(shm + k, num_elements_to_send, MPI_BYTE, dest, 0, MPI_COMM_WORLD, &req); 
	}
}

//TODO: refactor offset_str and offset

void serve_remote_read(const char* path_c, const char* offset_str, int dest, long int offset, long int nbytes) {
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
		std::cerr << "error capio_helper file " << path_c << " not in shared memory" << std::endl;
		exit(1);
	}
	nbytes = file_size - offset;
	std::string nbytes_str = std::to_string(nbytes);
	const char* nbytes_cstr = nbytes_str.c_str();
	const char* s1 = "sending";
	const size_t len1 = strlen(s1);
	const size_t len2 = strlen(path_c);
	const size_t len3 = strlen(offset_str);
	const size_t len4 = strlen(nbytes_cstr);
	buf_send = (char*) malloc((len1 + len2 + len3 + len4 + 4) * sizeof(char));//TODO:add malloc check
	sprintf(buf_send, "%s %s %s %s", s1, path_c, offset_str, nbytes_cstr);
	#ifdef CAPIOLOG
		std::cout << "helper serve remote read msg sent: " << buf_send << " to " << dest << std::endl;
	#endif
	//send warning
	MPI_Send(buf_send, strlen(buf_send) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
	free(buf_send);
	//send data
	#ifdef MYDEBUG
	int* tmp = (int*) malloc(*size_shm);
	std::cout << "helper sending " << *size_shm << " bytes" << std::endl;
	memcpy(tmp, file_shm, *size_shm); 
	for (int i = 0; i < *size_shm / sizeof(int); ++i) {
		std::cout << "helper sending tmp[i] " << tmp[i] << std::endl;
	}
	free(tmp);
	#endif
	#ifdef CAPIOLOG
		std::cout << "before sending part of the file to : " << dest << " with offset " << offset << " nbytes" << nbytes << std::endl;
	#endif
	send_file(((char*) file_shm) + offset, nbytes, dest);
	#ifdef CAPIOLOG
		std::cout << "after sending part of the file to : " << dest << std::endl;
	#endif
	sem_post(&remote_read_sem);
}

void* wait_for_data(void* pthread_arg) {
	struct remote_read_metadata* rr_metadata = (struct remote_read_metadata*) pthread_arg;
	const char* path = rr_metadata->path;
	long int offset = rr_metadata->offset;
	int dest = rr_metadata->dest;
	long int nbytes = rr_metadata->nbytes;
	const char * offset_str = std::to_string(offset).c_str();
			#ifdef CAPIOLOG
				std::cout << "wait for data before" << std::endl;
			#endif
	sem_wait(rr_metadata->sem);
	serve_remote_read(path, offset_str, dest, offset, nbytes);
	free(rr_metadata->sem);
	free(rr_metadata->path);
	free(rr_metadata);
			#ifdef CAPIOLOG
				std::cout << "wait for data after" << std::endl;
			#endif
	return nullptr;
}


bool data_avaiable(const char* path_c, long int offset, long int nbytes_requested) {
	long int file_size = *std::get<1>(files_metadata[path_c]);
	return offset + nbytes_requested <= file_size;
}

void lightweight_MPI_Recv(char* buf_recv, MPI_Status* status) {
	MPI_Request request;
	int received = 0;
	MPI_Irecv(buf_recv, 2048, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &request); //receive from server
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
	for (long int k = 0; k < bytes_expected; k += bytes_received) {
		MPI_Recv(shm + k, bytes_expected - k, MPI_BYTE, source, 0, MPI_COMM_WORLD, &status);//TODO; 4096 should be a parameter
		MPI_Get_count(&status, MPI_CHAR, &bytes_received); //why in recv MPI_BYTE while ehre MPI_CHAR?
	}
}

void* capio_helper(void* pthread_arg) {
	char buf_recv[2048];
	MPI_Status status;
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	sem_wait(&internal_server_sem);
		#ifdef CAPIOLOG
		#endif
	while(true) {
		lightweight_MPI_Recv(buf_recv, &status); //receive from server
		bool remote_request_to_read = strncmp(buf_recv, "read", 4) == 0;
		if (remote_request_to_read) {
		#ifdef CAPIOLOG
		std::cout << "helper remote req to read " << buf_recv << std::endl;
		#endif
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
				#ifdef CAPIOLOG
					std::cout << "helper data avaiable" << std::endl;
				#endif
				serve_remote_read(path_c, offset_str, dest, offset, nbytes);
			}
			else {
				#ifdef CAPIOLOG
					std::cout << "helper data not avaiable" << std::endl;
				#endif
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
			#ifdef CAPIOLOG
				std::cout << "helper received sending msg" << std::endl;
			#endif
			off64_t bytes_received;
			int source = status.MPI_SOURCE;
			off64_t offset; 
			char path_c[1024];
			sscanf(buf_recv, "sending %s %ld %ld", path_c, &offset, &bytes_received);
			std::string path(path_c);
			void* file_shm; 
			if (files_metadata.find(path) != files_metadata.end()) {
				file_shm = std::get<0>(files_metadata[path]);
			}
			else {
				std::cerr << "error capio_helper file " << path << " not in shared memory" << std::endl;
				exit(1);
			}
			#ifdef CAPIOLOG
				std::cout << "helper before received part of the file from process " << source << std::endl;
				std::cout << "offset " << offset << std::endl;
				std::cout << "bytes received " << bytes_received << std::endl;
			#endif
			recv_file((char*)file_shm + offset, source, bytes_received);
			#ifdef CAPIOLOG
				std::cout << "helper received part of the file" << std::endl;
			#endif
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
			buf_requests->write(c_str, (strlen(c_str) + 1) * sizeof(char));
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
	int res = sem_init(&internal_server_sem, 0, 0);
	if (res !=0) {
		std::cerr << __FILE__ << ":" << __LINE__ << " - " << std::flush;
		perror("sem_init failed"); exit(res);
	}
	sem_init(&remote_read_sem, 0, 1);
	sem_init(&handle_remote_read_sem, 0, 1);
	sem_init(&handle_local_read_sem, 0, 1);
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
	res = sem_destroy(&remote_read_sem);
	if (res !=0) {
		std::cerr << __FILE__ << ":" << __LINE__ << " - " << std::flush;
		perror("sem_destroy failed"); exit(res);
	}
	MPI_Finalize();
	return 0;
}
