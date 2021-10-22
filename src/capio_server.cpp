#include<string>
#include<cstring>
#include<unordered_map>
#include <iostream>

#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <semaphore.h>

#include <mpi.h>

#include "utils/common.hpp"


// pid -> fd ->(file_shm, index)
std::unordered_map<int, std::unordered_map<int, std::pair<void*, long int>>> processes_files;

// pid -> fd -> pathname
std::unordered_map<int, std::unordered_map<int, std::string>> processes_files_metadata;

// pid -> (response shared buffer, index)
std::unordered_map<int, std::pair<void*, int>> response_buffers;

// pathname -> (file_shm, file_size)
std::unordered_map<std::string, std::pair<void*, long int*>> files_metadata;

// pathname -> node
std::unordered_map<std::string, char*> files_location;

// node -> rank
std::unordered_map<std::string, int> nodes_helper_rank;

// path -> (pid, fd, numbytes)
std::unordered_map<std::string, std::tuple<int, int, long int>>  remote_pending_reads;

//name of the node

char node_name[MPI_MAX_PROCESSOR_NAME];

circular_buffer buf_requests; 
sem_t* sem_requests;
sem_t* sem_new_msgs;
std::unordered_map<int, sem_t*> sems_response;
static int index_not_read = 0;


sem_t* create_sem_requests() {
	return sem_open("sem_requests", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 1);
}


//TODO: same function in capio_posix, to put in common
sem_t* get_sem_requests() {
	return sem_open("sem_requests", 0);
}

void* create_shm_circular_buffer(std::string shm_name) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
	struct stat sb;
	const long int size = 1024L * 1024 * 1024;
	if (fd == -1)
		err_exit("shm_open");
	if (ftruncate(fd, size) == -1)
		err_exit("ftruncate");
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap");
//	if (close(fd) == -1);
//		err_exit("close");
	return p;
}

int* create_shm_int(std::string shm_name) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
	struct stat sb;
	const int size = sizeof(int);
	if (fd == -1)
		err_exit("shm_open");
	if (ftruncate(fd, size) == -1)
		err_exit("ftruncate");
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap");
//	if (close(fd) == -1);
//		err_exit("close");
	return (int*) p;
}
long int* create_shm_long_int(std::string shm_name) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
	struct stat sb;
	const int size = sizeof(long int);
	if (fd == -1)
		err_exit("shm_open");
	if (ftruncate(fd, size) == -1)
		err_exit("ftruncate");
	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap");
//	if (close(fd) == -1);
//		err_exit("close");
	return (long int*) p;
}

struct circular_buffer create_circular_buffer() {
	//open shm
	void* buf = create_shm_circular_buffer("circular_buffer");
	int* i = (int*) create_shm_int("index_buf");
	*i = 0;
	circular_buffer br;
	br.buf = buf;
	br.i = i;
	br.k = 0;
	return br;
}

void write_file_location(const std::string& file_name, int rank, std::string path_to_write) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    int fd;
    if ((fd = open(file_name.c_str(), O_WRONLY|O_APPEND, 0664)) == -1) {
        std::cout << "writer " << rank << " error opening file, errno = " << errno << " strerror(errno): " << strerror(errno) << std::endl;
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
        std::cout << "write " << rank << "failed to lock the file" << std::endl;
    }
    int res, k = 0;
    int num_elements_written;
    
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
	res = write(fd, file_location, sizeof(char) * strlen(file_location));
    printf("wrote file location: %s \n", file_location);
	files_location[path_to_write] = node_name;
	// Now release the lock explicitly.
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLKW, &lock) == - 1) {
        std::cout << "write " << rank << "failed to unlock the file" << std::endl;
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
    lock.l_type = F_RDLCK;    /* read/write (exclusive) lock */
    lock.l_whence = SEEK_SET; /* base for seek offsets */
    lock.l_start = 0;         /* 1st byte in file */
    lock.l_len = 0;           /* 0 here means 'until EOF' */
    lock.l_pid = getpid();    /* process id */

    int fd; /* file descriptor to identify a file within a process */
    //fd = open(file_name.c_str(), O_RDONLY);  /* -1 signals an error */
    fp = fopen(file_name.c_str(), "r");
	if (fp == NULL) {
		std::cout << "capio server " << rank << " failed to open the location file" << std::endl;
		return false;
	}
	fd = fileno(fp);
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        std::cout << "capio server " << rank << " failed to lock the file" << std::endl;
        close(fd);
        return false;
    }
	const char* path_to_check_cstr = path_to_check.c_str();
	bool found = false;
    while ((read = getline(&line, &len, fp)) != -1 && !found) {
        printf("Retrieved line of length %zu:\n", read);
        printf("%s", line);
		char path[1024]; //TODO: heap memory
		int i = 0;
		while(line[i] != ' ') {
			path[i] = line[i];
			++i;
		}
		path[i] = '\0';
		//char* path = strtok_r(p_line, " ", &p_tmp);
		std::cout << "path " << path << std::endl;
		//char* node_str = strtok_r(NULL, "\n", &p_tmp);
		char node_str[1024]; //TODO: heap memory 
		++i;
		int j = 0;
		while(line[i] != '\n') {
			node_str[j] = line[i];
			++i;
			++j;
		}
		node_str[j] = '\0';
		printf("node %s\n", node_str);
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
        std::cout << "reader " << rank << " failed to unlock the file" << std::endl;
        res = false;
    }
    fclose(fp);
    return res;
}

void read_next_msg(int rank) {
	sem_wait(sem_new_msgs);
	char str[4096];
	std::fill(str, str + 4096, 0);
	//memcpy(buf_requests.buf, pathname, strlen(pathname));
	int k = buf_requests.k;
	std::cout << "k = " << k << std::endl;
	bool is_open = false;
	int i = 0;
	while (((char*)buf_requests.buf)[k] != '\0') {
		str[i] = ((char*) buf_requests.buf)[k];
		++k;
		++i;
	}
	str[k] = ((char*) buf_requests.buf)[k];
	buf_requests.k = k + 1;
	char* p = str;
	printf("msg read after loop: %s\n", str);
	index_not_read += strlen(str) + 1;
	is_open = strncmp(str, "open", 4) == 0;
	std::cout << "is_open " << is_open << std::endl;
	int pid;
	if (is_open) {
		pid = strtol(str + 5, &p, 10);
		std::cout << "pid " << pid << std::endl;
		if (sems_response.find(pid) == sems_response.end()) {
			std::cout << "opening sem_response" << std::endl;
			sems_response[pid] = sem_open(("sem_response" + std::to_string(pid)).c_str(), O_RDWR);
			if (sems_response[pid] == SEM_FAILED) {
				std::cout << "error creating the response semafore for pid " << pid << std::endl;  	
			}
			response_buffers[pid].first = (int*) get_shm("buf_response" + std::to_string(pid));
			response_buffers[pid].second = 0; 

		}
		std::string path(p + 1);
		std::cout << "path file " << path << std::endl;
		int fd = open(path.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IRWXU);
		if (fd == -1) {
			std::cout << "capio server, error to open the file " << path << std::endl;
			MPI_Finalize();
			exit(1);
		}
		processes_files[pid][fd] = std::pair<void*, int>(create_shm(path, 1024L * 1024 * 1024* 2), 0); //TODO: what happens if a process open the same file twice?
		std::cout << "before post sems_respons" << std::endl;
		std::cout << sems_response[pid] << std::endl;
		std::pair<void*, int> tmp_pair = response_buffers[pid];
		((int*) tmp_pair.first)[tmp_pair.second] = fd; 
		++response_buffers[pid].second;
		std::cout << "before post sems_respons 2" << std::endl;
		sem_post(sems_response[pid]);
		std::cout << "after post sems_respons" << std::endl;
		if (files_metadata.find(path) == files_metadata.end()) {
			std::cout << "server " << rank << "updating file metadata for " << path << std::endl;
			files_metadata[path].first = processes_files[pid][fd].first;	
			std::cout<< "debug 1" << std::endl;
			files_metadata[path].second = create_shm_long_int(path + "_size");	
			std::cout<< "debug 2" << std::endl;
			if (files_metadata.find(path) == files_metadata.end()) {//debug
				std::cout << "server " << rank << " error updating" <<std ::endl;
				exit(1);
			}
			std::cout<< "debug 3" << std::endl;
		}
			std::cout<< "debug 4" << std::endl;
		processes_files_metadata[pid][fd] = path;
			std::cout<< "debug 5" << std::endl;
	}
	else {
		bool is_write = strncmp(str, "writ", 4) == 0;
		if (is_write) {
			//check if another process is waiting for this data
			std::cout << "server handling a write" << std::endl;
			int pid = strtol(str + 5, &p, 10);;
			int fd = strtol(p, &p, 10);
			long int data_size = strtol(p, &p, 10);
			std::cout << "pid " << pid << std::endl;
			std::cout << "fd " << fd << std::endl;
			std::cout << "data_size " << data_size << std::endl;
			
			if (processes_files[pid][fd].second == 0) {
				write_file_location("files_location.txt", rank, processes_files_metadata[pid][fd]);
				std::cout << "wrote files_location.txt " << std::endl;
			}
			
			processes_files[pid][fd].second += data_size;
			std::string path = processes_files_metadata[pid][fd];
			*files_metadata[path].second += data_size; //works only if there is only one writer at time	for each file
			//char* tmp = (char*) malloc(data_size);
			//memcpy(tmp, processes_files[pid][fd].first, data_size); 
			//for (int i = 0; i < data_size; ++i) {
			//	std::cout << "tmp[i] " << tmp[i] << std::endl;
			//}
			//free(tmp);
		}
		else {
			bool is_read = strncmp(str, "read", 4) == 0;
			if (is_read) {
				std::cout << "server handling a read" << std::endl;
				int pid = strtol(str + 5, &p, 10);;
				int fd = strtol(p, &p, 10);
				long int count = strtol(p, &p, 10);
				std::cout << "pid " << pid << std::endl;
				std::cout << "fd " << fd << std::endl;
				std::cout << "count " << count << std::endl;
				
				if (processes_files[pid][fd].second == 0 && files_location.find(processes_files_metadata[pid][fd]) == files_location.end()) {
					check_remote_file("files_location.txt", rank, processes_files_metadata[pid][fd]);
					std::cout << "read files_location.txt " << std::endl;
					if (files_location.find(processes_files_metadata[pid][fd]) == files_location.end()) {
						std::cout << "error read before relative write" << std::endl;
						exit(1);

					}
				}
				if (files_location[processes_files_metadata[pid][fd]] == node_name) {
					std::cout << "read local file" << std::endl;
					#ifdef MYDEBUG
					int* tmp = (int*) malloc(count);
					memcpy(tmp, processes_files[pid][fd].first + processes_files[pid][fd].second, count); 
					for (int i = 0; i < count / sizeof(int); ++i) {
						std::cout << "server local read tmp[i] " << tmp[i] << std::endl;
					}
				
					free(tmp);
					#endif
					std::pair<void*, int> tmp_pair = response_buffers[pid];
					((int*) tmp_pair.first)[tmp_pair.second] = processes_files[pid][fd].second; 
					processes_files[pid][fd].second += count;
					//TODO: check if there is data that can be read in the local memory file
					++response_buffers[pid].second;
					sem_post(sems_response[pid]); 
				}
				else {
					const char* msg;
					std::string str_msg;
					std::cout << " files_location[processes_files_metadata[pid][fd]] " << files_location[processes_files_metadata[pid][fd]] << std::endl;
					int dest = nodes_helper_rank[files_location[processes_files_metadata[pid][fd]]];
					long int offset = processes_files[pid][fd].second;
					str_msg = "read " + processes_files_metadata[pid][fd] + " " + std::to_string(rank + 1) + " " + std::to_string(offset) + " " + std::to_string(count); 
					msg = str_msg.c_str();
					MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
					printf("server msg MPI_Send %s\n", msg);
					std::cout << "msg sent to dest " << dest << std::endl;
					std::cout << "read remote file" << std::endl;
					remote_pending_reads[processes_files_metadata[pid][fd]] = std::make_tuple(pid, fd, count);
				}
			}
			else {
				bool is_close = strncmp(str, "clos", 4) == 0;
				if (is_close) { //TODO: more cleaning
					std::cout << "server handling close" << std::endl;
					int pid = strtol(str + 5, &p, 10);;
					int fd = strtol(p, &p, 10);
					std::cout << "pid " << pid << std::endl;
					std::cout << "fd " << fd << std::endl;
				

					if (close(fd) == -1) {
						std::cout << "capio server, error: impossible close the file with fd = " << fd << std::endl;
						MPI_Finalize();
						exit(1);
					}
				}
				else {
					bool is_remote_read = strncmp(str, "ream", 4) == 0;
					if (is_remote_read) {
						long int bytes_received;
						std::string tmpstr(str + 5);	
						std::string path = tmpstr.substr(0, tmpstr.find(" ")); // token is "scott"
						bytes_received = std::stoi(tmpstr.substr(path.length() + 1));
						std::cout << "server " << rank << " path " << path << " bytes_received " << bytes_received << std::endl;
						int pid, fd;
						long int count;
						pid = std::get<0>(remote_pending_reads[path]);
						fd = std::get<1>(remote_pending_reads[path]);
						count = std::get<2>(remote_pending_reads[path]);
						//this part is equals to the local read (TODO: function)
						#ifdef MYDEBUG
						int* tmp = (int*) malloc(count);
						memcpy(tmp, processes_files[pid][fd].first + processes_files[pid][fd].second, count); 
						for (int i = 0; i < count / sizeof(int); ++i) {
							std::cout << "server remote read tmp[i] " << tmp[i] << std::endl;
						}
				
						free(tmp);
						#endif
						std::pair<void*, int> tmp_pair = response_buffers[pid];
						((int*) tmp_pair.first)[tmp_pair.second] = processes_files[pid][fd].second; 
						processes_files[pid][fd].second += count;
						//TODO: check if there is data that can be read in the local memory file
						++response_buffers[pid].second;
						sem_post(sems_response[pid]); 
						*files_metadata[path].second += bytes_received;

					}
					else {
						std::cout << "error msg read" << std::endl;
						MPI_Finalize();
						exit(1);
					}
				}
			}
		}
	}
	std::cout << "end request" << std::endl;
	    //sem_post(sem_requests);
	return;
}

void capio_server(int rank) {
	buf_requests = create_circular_buffer();
	sem_requests = create_sem_requests();
	sem_new_msgs = sem_open("sem_new_msgs", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	MPI_Send(NULL, 0, MPI_INT, rank + 1, 0, MPI_COMM_WORLD);
	while(true) {
		std::cout << "serving" << std::endl;
		read_next_msg(rank);

		//respond();
	}
}

void capio_helper(int rank) {
	char buf_recv[2048];
	char* buf_send;
	MPI_Status status;
	MPI_Recv(NULL, 0, MPI_INT, rank - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	buf_requests = get_circular_buffer();
	sem_requests = get_sem_requests();
	sem_new_msgs = sem_open("sem_new_msgs", O_RDWR);
	while(true) {
		std::cout << "helper" << std::endl;
		MPI_Recv(buf_recv, 2048, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status); //receive from server
		std::cout << "helper rank " << rank << " msg received" << buf_recv << std::endl;
		bool remote_request_to_read = strncmp(buf_recv, "read", 4) == 0;
		if (remote_request_to_read) {
			char** p_tmp;
			std::cout << "before strtok" << std::endl;
			char path_c[512];
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
			while (buf_recv[j] != '\0') {
				dest_str[j] = buf_recv[i];
				++i;
				++j;
			}
			dest_str[j] = '\0';
			int dest = std::atoi(dest_str);
			std::cout << "helper " << rank << " path " << path_c << std::endl;
			std::cout << "helper " << rank << " dest " << dest << std::endl;
			//int offset; at the moment ignored
			//int count; at the moment ignored
			const char* s1 = "sending ";
			const size_t len1 = strlen(s1);
			const size_t len2 = strlen(path_c);
			// in real code you would check for errors in malloc here
			buf_send = (char*) malloc((len1 + len2 + 1) * sizeof(char));
			memcpy(buf_send, s1, len1);
			memcpy(buf_send + len1, path_c, len2 + 1); // +1 to copy the null-terminator
		    printf("helper warning sent: %s\n", buf_send);
			std::cout << "helper dest warning = " << dest << std::endl;
			//send warning
			MPI_Send(buf_send, strlen(buf_send) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
			std::cout << "helper sending data" << std::endl;
			free(buf_send);
			//send data
			void* file_shm = get_shm(path_c);
			int* size_shm = (int*) get_shm(std::string(path_c) + "_size");
			#ifdef MYDEBUG
			int* tmp = (int*) malloc(*size_shm);
			std::cout << "helper sending " << *size_shm << " bytes" << std::endl;
			memcpy(tmp, file_shm, *size_shm); 
			for (int i = 0; i < *size_shm / sizeof(int); ++i) {
				std::cout << "helper sending tmp[i] " << tmp[i] << std::endl;
			}
				
			free(tmp);
			#endif
			MPI_Send(file_shm, *size_shm, MPI_BYTE, dest, 0, MPI_COMM_WORLD); 
		}
		else if(strncmp(buf_recv, "sending", 7) == 0) { //receiving a file
			std::string path(buf_recv + 8);
			void* file_shm =  get_shm(path);
			int bytes_received;
			int source = status.MPI_SOURCE;
			std::cout << "helper receiving data file " << path << " from process rank " << source << std::endl;
			MPI_Recv(file_shm, 1024L * 1024 * 1024, MPI_BYTE, source, 0, MPI_COMM_WORLD, &status);//TODO; 4096 should be a parameter
			MPI_Get_count(&status, MPI_CHAR, &bytes_received);
			bytes_received *= sizeof(char);
			std::cout << "helper " << rank << " sending wake up call to my server" << std::endl;
			#ifdef MYDEBUG
			int* tmp = (int*) malloc(bytes_received);
			memcpy(tmp, file_shm, bytes_received); 
			for (int i = 0; i < bytes_received / sizeof(int); ++i) {
				std::cout << "helper receiving tmp[i] " << tmp[i] << std::endl;
			}	
			free(tmp);
			#endif
			std::string msg = "ream " + path + + " " + std::to_string(bytes_received);
			sem_wait(sem_requests);
			const char* c_str = msg.c_str();
			memcpy(buf_requests.buf + *buf_requests.i, c_str, strlen(c_str) + 1);
			std::cout << "open before response" << std::endl;
			char tmp_str[1024];
			printf("c_str %s, len c_str %i\n", c_str, strlen(c_str));
			sprintf(tmp_str, "%s", (char*) buf_requests.buf + *buf_requests.i);
			printf("add open msg sent: %s\n", tmp_str);
			*buf_requests.i = *buf_requests.i + strlen(c_str) + 1;
			std::cout << "*buf_requests.i == " << *buf_requests.i << std::endl;
			sem_post(sem_requests);
			sem_post(sem_new_msgs);

		}
		else {
			std::cout << "helper error receiving message" << std::endl;
		}
	}
}


void handshake_servers(int rank, int size) {
	char* buf;	
	for (int i = 0; i < size; i += 2) {
		if (i != rank) {
			MPI_Send(node_name, strlen(node_name), MPI_CHAR, i, 0, MPI_COMM_WORLD); //TODO: possible deadlock
			buf = (char*) malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));//TODO: free
			MPI_Recv(buf, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			nodes_helper_rank[buf] = i + 1;//store the rank of the helper not the server
		}
	}
}

int main(int argc, char** argv) {
	int rank, size, len;
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Get_processor_name(node_name, &len);
	std::cout << "processor name " << node_name << std::endl;
	if (rank % 2 == 0) {
		handshake_servers(rank, size);
	    for (auto const &pair: nodes_helper_rank) {
			std::cout << "{" << pair.first << ": " << pair.second << "}\n";
		}
		std::cout << "capio server, rank " << rank << std::endl;
		capio_server(rank);
	}
	else {
		std::cout << "capio helper, rank " << rank << std::endl;
		capio_helper(rank);
	}	
	MPI_Finalize();
	return 0;
}
