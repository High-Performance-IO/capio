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

struct circular_buffer {
	void* buf;
	int* i;
	int k;
};

int next_fd = -1;

// pid -> fd ->(file_shm, index)
std::unordered_map<int, std::unordered_map<int, std::pair<void*, int>>> processes_files;

// pid -> fd -> pathname
std::unordered_map<int, std::unordered_map<int, std::string>> processes_files_metadata;

// pid -> (response shared buffer, index)
std::unordered_map<int, std::pair<void*, int>> response_buffers;

// pathname -> file_size
std::unordered_map<std::string, int> files_metadata;

// pathname -> node
std::unordered_map<std::string, char*> files_location;

//name of the node

char node_name[MPI_MAX_PROCESSOR_NAME];

circular_buffer buf_requests; 
sem_t* sem_requests;
sem_t* sem_new_msgs;
std::unordered_map<int, sem_t*> sems_response;
static int index_not_read = 0;

void err_exit(std::string error_msg) {
	std::cout << "error: " << error_msg << std::endl;
	exit(1);
}

sem_t* create_sem_requests() {
	return sem_open("sem_requests", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 1);
}

void* get_shm(std::string shm_name) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_RDWR, 0); //to be closed
	struct stat sb;
	if (fd == -1)
		err_exit("shm_open");
	/* Open existing object */
	/* Use shared memory object size as length argument for mmap()
	and as number of bytes to write() */
	if (fstat(fd, &sb) == -1)
		err_exit("fstat");
	p = mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err_exit("mmap");
//	if (close(fd) == -1);
//		err_exit("close");
	return p;
}

void* create_shm(std::string shm_name) {
	void* p = nullptr;
	// if we are not creating a new object, mode is equals to 0
	int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
	struct stat sb;
	const int size = 4096;
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

struct circular_buffer create_circular_buffer() {
	//open shm
	void* buf = create_shm("circular_buffer");
	int* i = (int*) create_shm("index_buf");
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
    fp = fopen("/etc/motd", "r");
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
	char** p_tmp;
	const char* path_to_check_cstr = path_to_check.c_str();
	bool found = false;
    while ((read = getline(&line, &len, fp)) != -1 && !found) {
        printf("Retrieved line of length %zu:\n", read);
		char* path = strtok_r(line, " ", p_tmp);
		char* node_str = strtok_r(NULL, " ", p_tmp);
        printf("%s", line);
		std::cout << "path " << path << std::endl;
		std::cout << "node " << node_str << std::endl;
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
	char str[1024];
	std::fill(str, str + 1024, 0);
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
		int fd = next_fd;
		--next_fd;
		processes_files[pid][fd] = std::pair<void*, int>(create_shm(path), 0); //TODO: what happens if a process open the same file twice?
		std::cout << "before post sems_respons" << std::endl;
		std::cout << sems_response[pid] << std::endl;
		std::pair<void*, int> tmp_pair = response_buffers[pid];
		((int*) tmp_pair.first)[tmp_pair.second] = fd; 
		++response_buffers[pid].second;
		sem_post(sems_response[pid]);
		if (files_metadata.find(path) == files_metadata.end()) {
			files_metadata[path] = 0;	
		}
		processes_files_metadata[pid][fd] = path;
	}
	else {
		bool is_write = strncmp(str, "writ", 4) == 0;
		if (is_write) {
			//check if another process is waiting for this data
			std::cout << "server handling a write" << std::endl;
			int pid = strtol(str + 5, &p, 10);;
			int fd = strtol(p, &p, 10);
			int data_size = strtol(p, &p, 10);
			std::cout << "pid " << pid << std::endl;
			std::cout << "fd " << fd << std::endl;
			std::cout << "data_size " << data_size << std::endl;
			
			if (processes_files[pid][fd].second == 0) {
				write_file_location("files_location.txt", rank, processes_files_metadata[pid][fd]);
				std::cout << "wrote files_location.txt " << std::endl;
			}
			
			processes_files[pid][fd].second += data_size;
			std::string path = processes_files_metadata[pid][fd];
			files_metadata[path] += data_size; //works only if there is only one writer at time	
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
				int count = strtol(p, &p, 10);
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
					int* tmp = (int*) malloc(count);
					memcpy(tmp, processes_files[pid][fd].first + processes_files[pid][fd].second, count); 
					for (int i = 0; i < count / sizeof(int); ++i) {
						std::cout << "tmp[i] " << tmp[i] << std::endl;
					}
				
					free(tmp);
					std::pair<void*, int> tmp_pair = response_buffers[pid];
					((int*) tmp_pair.first)[tmp_pair.second] = processes_files[pid][fd].second; 
					processes_files[pid][fd].second += count;
					//TODO: check if there is data that can be read in the local memory file
					++response_buffers[pid].second;
					sem_post(sems_response[pid]); 
				}
				else {
					std::cout << "read remote file" << std::endl;
				}
			}
			else {
				bool is_close = strncmp(str, "clos", 4) == 0;
				if (is_close) {
					std::cout << "server handling close" << std::endl;
				}
				else {
					std::cout << "error msg read" << std::endl;
					MPI_Finalize();
					exit(1);
				}
			}
		}
	}
	    //sem_post(sem_requests);
}

void capio_server(int rank) {
	buf_requests = create_circular_buffer();
	sem_requests = create_sem_requests();
	sem_new_msgs = sem_open("sem_new_msgs", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	while(true) {
		std::cout << "serving" << std::endl;
		read_next_msg(rank);

		//respond();
	}
}

void capio_helper(int rank) {

}

int main(int argc, char** argv) {
	int rank, len;
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Get_processor_name(node_name, &len);
	std::cout << "processor name " << node_name << std::endl;
	if (rank % 2 == 0) {
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
