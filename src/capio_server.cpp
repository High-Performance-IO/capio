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

void read_next_msg() {
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
		int fd = next_fd; //TODO: it works only for one file
		--next_fd;
		processes_files[pid][fd] = std::pair<void*, int>(create_shm(path), 0); //TODO: what happens if a process open the same file twice?
		sem_post(sem_requests);
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
				bool is_close = strncmp(str, "clos", 4) == 0;
				if (is_close) {
					std::cout << "server handling close" << std::endl;
				}
			}
		}
		pid = strtol(str + 5, NULL, 10);
	    sem_post(sem_requests);
	}
	//sem_post pid response
}

int main(int argc, char** argv) {
	buf_requests = create_circular_buffer();
	sem_requests = create_sem_requests();
	sem_new_msgs = sem_open("sem_new_msgs", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
	while(true) {
		std::cout << "serving" << std::endl;
		read_next_msg();

		//respond();
	}
	return 0;
}
