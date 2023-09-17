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

std::ofstream logfile;

#include "capio/circular_buffer.hpp"
#include "capio/filesystem.hpp"
#include "capio/spsc_queue.hpp"

#include "utils/capio_file.hpp"
#include "utils/common.hpp"
#include "utils/data_structure.hpp"
#include "utils/env.hpp"
#include "utils/json.hpp"


using namespace simdjson;

std::string* capio_dir = nullptr;

bool shm_full = false;
long int total_bytes_shm = 0;

MPI_Request req;
int n_servers;
int fd_files_location;

// [(fd, fp, bool), ....] the third argument is true if the last time getline returned -1, false otherwise
CSFDFileLocationReadsVector_t fd_files_location_reads;

/*
 * For multithreading:
 * tid -> pid*/
CSPidsMap_T pids;

// tid -> application name 
CSAppsMap_t apps;

// application name -> set of files already sent
CSFilesSentMap_t files_sent;

// tid -> fd ->(capio_file, index)
CSProcessFileMap_t processes_files;

// tid -> fd -> pathname
CSProcessFileMetadataMap_t processes_files_metadata;

// tid -> (response shared buffer, index)
CSResponseBufferMap_t response_buffers;

// tid -> (client_to_server_data_buf, server_to_client_data_buf)
CSDataBufferMap_t data_buffers;

/* pathname ->  capio_file*/
CSFilesMetadata_t files_metadata;

// path -> (committed, mode, app_name, n_files, bool, n_close)
CSMetadataConfMap_t metadata_conf;

// [(glob, committed, mode, app_name, n_files, batch_size, permanent, n_close), (glob, committed, mode, app_name, n_files, batch_size, permanent, n_close), ...]
CSMetadataConfGlobs_t metadata_conf_globs;

/*
 * pid -> pathname -> bool
 * Different threads with the same pid are threated as a single writer
 */
CSWritersMap_t writers;

// pathname -> (node, offset)
CSFilesLocationMap_t files_location;

// node -> rank
CSNodesHelperRankMap_t nodes_helper_rank;

//rank -> node
CSRankToNodeMap_t rank_to_node;

/*
 * It contains all the reads requested by local processes to read files that are in the local node for which the data is not yet avaiable.
 * path -> [(tid, fd, numbytes, is_getdents), ...]
 */
CSPendingReadsMap_t  pending_reads;

/*
 * It contains all the reads requested to the remote nodes that are not yet satisfied 
 * path -> [(tid, fd, numbytes, is_getdents), ...]
 */

CSMyRemotePendingReads_t my_remote_pending_reads;

/*
 * It contains all the stats requested to the remote nodes that are not yet satisfied 
 * path -> [tid1, tid2, tid3, ...]
 */

CSMyRemotePendingStats_t  my_remote_pending_stats;

/*
 * It contains all the read requested by other nodes for which the data is not yet avaiable 
 * path -> [(offset, numbytes, sem_pointer), ...]
 */

CSClientsRemotePendingReads_t clients_remote_pending_reads;

CSClientsRemotePendingStats_t clients_remote_pending_stat;

// it contains the file saved on disk
CSOnDiskMap_t on_disk;

CSClientsRemotePendingNFilesMap_t clients_remote_pending_nfiles;

// name of the node

char node_name[MPI_MAX_PROCESSOR_NAME];

CSCircularBuff_t* buf_requests;
//std::unordered_map<int, sem_t*> sems_response;
CSSemsWriteMap_t* sems_write;

sem_t internal_server_sem;
sem_t remote_read_sem;
sem_t handle_remote_read_sem;
sem_t handle_remote_stat_sem;
sem_t handle_local_read_sem;
sem_t files_metadata_sem;
sem_t files_location_sem;
sem_t clients_remote_pending_nfiles_sem;


#include "handlers.hpp"


// std::unordered_map<int, std::unordered_map<int, std::string>> processes_files_metadata;
// std::unordered_map<std::string, std::tuple<void*, off64_t*, off64_t, bool, Capio_file>> files_metadata;
// std::unordered_map<int, std::unordered_map<std::string, bool>> writers;
// pathname -> node
//std::unordered_map<std::string, char*> files_location;
//std::unordered_map<std::string, std::vector<std::tuple<int, int, off64_t>>>  pending_reads;

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
	return;
}



void handshake_servers(int rank) {
	char* buf;	
	buf = (char*) malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
	if (buf == NULL)
		err_exit("malloc handshake_servers", logfile);
	if (rank == 0) {
		clean_files_location(n_servers);
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
	open_files_metadata(rank, &fd_files_location);
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

    off64_t prefetch_data_size = get_prefetch_data_size();

	if (prefetch_data_size != 0 && nbytes > prefetch_data_size) {
        nbytes = prefetch_data_size;
    }

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

void* wait_for_data(void* pthread_arg) {
	struct remote_read_metadata* rr_metadata = (struct remote_read_metadata*) pthread_arg;
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


int find_batch_size(std::string glob) {
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

void send_n_files(std::string prefix, std::vector<std::string>* files_to_send, int n_files, int dest) {
		std::string msg = "nsend " + prefix;
		#ifdef CAPIOLOG
		logfile << "send_n_files " << std::endl;
		#endif
		size_t prefix_length = prefix.length();
		for (std::string path : *files_to_send) {
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
		for (std::string path : *files_to_send) {
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
	struct remote_n_files* rr_metadata = (struct remote_n_files*) arg;
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

std::vector<std::string>* files_avaiable(std::string prefix, std::string app, std::string path_file, int n_files) {
	int n_files_completed = 0;
	size_t prefix_length = prefix.length();
	std::vector<std::string>* files_to_send = new std::vector<std::string>;	
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
		struct remote_n_files* rr_metadata = (struct remote_n_files*) malloc(sizeof(struct remote_n_files));
		#ifdef CAPIOLOG
		logfile << "waiting n files" << std::endl;
		#endif
		rr_metadata->prefix = (char*) malloc(sizeof(char) * PATH_MAX);
		if (rr_metadata->prefix == NULL)
			err_exit("malloc 1 in helper_nreads_req", logfile);
		strcpy(rr_metadata->prefix, prefix);
		rr_metadata->dest = dest;
		rr_metadata->sem = (sem_t*) malloc(sizeof(sem_t));
		if (rr_metadata->sem == NULL)
			err_exit("malloc 2 in helper_nreads_req", logfile);
		rr_metadata->files_path = files;
		rr_metadata->n_files = n_files;
		int res = sem_init(rr_metadata->sem, 0, 0);
		if (res == -1)
			err_exit("sem_init rr_metadata->sem in helper_nreads_req", logfile);

		res = pthread_create(&t, NULL, wait_for_n_files, (void*) rr_metadata);

		if (res != 0) {
			logfile << "error creation of src server thread wait for completion" << std::endl;
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
	struct remote_stat_metadata* rr_metadata = (struct remote_stat_metadata*) pthread_arg;
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
	if (path_c == NULL)
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
	struct remote_stat_metadata* rr_metadata = (struct remote_stat_metadata*) malloc(sizeof(struct remote_stat_metadata));

	rr_metadata->path = (char*) malloc(sizeof(char) * PATH_MAX);
	strcpy(rr_metadata->path, path_c);

	rr_metadata->c_file = &c_file;
	rr_metadata->dest = dest;
	rr_metadata->sem = (sem_t*) malloc(sizeof(sem_t));
	if (rr_metadata->sem == NULL)
		err_exit("malloc 2 in helper_stat_req", logfile);
	int res = sem_init(rr_metadata->sem, 0, 0);

	if (res == -1) 
		err_exit("sem_init rr_metadata->sem in helper_stat_req", logfile);
	#ifdef CAPIOLOG
	logfile << "before pthread_create" << std::endl;
	#endif
				res = pthread_create(&t, NULL, wait_for_completion, (void*) rr_metadata);
	#ifdef CAPIOLOG
	logfile << "after pthread create" << std::endl;
	#endif
				if (res != 0) {
					logfile << "error creation of src server thread wait for completion" << std::endl;
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

void recv_nfiles(char* buf_recv, int source) {
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
		files.push_back(std::make_pair(path, bytes_received));
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
				p_shm = expand_memory_for_file(path, file_size, *c_file);
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
	if (buf_recv == NULL)
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
			if (path_c == NULL)
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
				if (rr_metadata == NULL)
					err_exit("malloc 3 in capio_helper", logfile);
				strcpy(rr_metadata->path, path_c);
				rr_metadata->offset = offset;
				rr_metadata->dest = dest;
				rr_metadata->nbytes = nbytes;
				rr_metadata->complete = &(c_file.complete);
				rr_metadata->sem = (sem_t*) malloc(sizeof(sem_t));
				if (rr_metadata->sem == NULL)
					err_exit("malloc 4 in capio_helper", logfile);
				int res = sem_init(rr_metadata->sem, 0, 0);
				if (res == -1)
					err_exit("sem_init rr_metadata->sem", logfile); 
				res = pthread_create(&t, NULL, wait_for_data, (void*) rr_metadata);
				if (res != 0) {
					logfile << "error creation of src server thread helper loop wait for data" << std::endl;
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
			if (it != files_metadata.end()) {
				Capio_file& c_file = *(it->second);
				if (c_file.buf_to_allocate()) {

        #ifdef CAPIOLOG
        logfile << "allocating file " << path << std::endl;
        #endif
					c_file.create_buffer(path, false);
					}
				file_shm = c_file.get_buffer();
				sem_post(&files_metadata_sem);
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

				Capio_file& c_file = *(it->second);
				off64_t file_shm_size = c_file.get_buf_size();
				off64_t file_size = offset + bytes_received;
				if (file_size > file_shm_size) {
					file_shm = expand_memory_for_file(path, file_size, c_file);
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
	if (argc == 3) {
		conf_file = argv[2];
		parse_conf_file(conf_file, &metadata_conf_globs, &metadata_conf, capio_dir);
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
	res = pthread_create(&server_thread, NULL, capio_server, &rank);
	if (res != 0) {
		logfile << "error creation of src server main thread" << std::endl;
		MPI_Finalize();
		return 1;
	}
	res = pthread_create(&helper_thread, NULL, capio_helper, nullptr);
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