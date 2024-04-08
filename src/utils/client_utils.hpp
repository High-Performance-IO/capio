#ifndef CAPIO_CLIENT_UTILS_HPP_
#define CAPIO_CLIENT_UTILS_HPP_

#include "../spsc_queue.hpp"
#include "common.hpp"
#include <cstdarg>
FILE* logfile = nullptr;


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
    vfprintf(logfile, p, argp);
    va_end(argp);
    fflush(logfile);
}

void write_shm(SPSC_queue<char>* data_buf, const void* buffer, off64_t count) {	
	size_t n_writes = count / *WINDOW_DATA_BUFS;
	size_t r = count % *WINDOW_DATA_BUFS;
	#ifdef CAPIOLOG
	CAPIO_DBG("write shm %ld %ld\n", n_writes, r);
	if (r)
		CAPIO_DBG("lol shm %ld %ld\n", n_writes, r);
	#endif
	size_t i = 0;
	while (i < n_writes) {
		data_buf->write((char*) buffer + i * *WINDOW_DATA_BUFS);
		++i;
	}
	if (r) {
		#ifdef CAPIOLOG
		CAPIO_DBG("write shm debug before\n");
		#endif
		data_buf->write((char*) buffer + i * *WINDOW_DATA_BUFS, r);
		#ifdef CAPIOLOG
		CAPIO_DBG("write shm debug after\n");
		#endif
	}
}

void add_write_request(long int my_tid, int fd, off64_t count, off64_t old_offset, Circular_buffer<char>* buf_reqs) { //da modifcare con capio_file sia per una normale scrittura sia per quando si fa il batch
	char c_str[256];
	sprintf(c_str, "writ %ld %d %ld %ld", my_tid, fd, old_offset, count);
	buf_reqs->write(c_str, 256 * sizeof(char));
}

off64_t add_read_request(long int tid, int fd, off64_t count, Circular_buffer<char>* buf_reqs, Circular_buffer<off_t>* buf_response) {
	char c_str[256];
	sprintf(c_str, "read %ld %d %ld", tid, fd, count);
	buf_reqs->write(c_str, 256 * sizeof(char));
	off64_t offset_upperbound;
	buf_response->read(&offset_upperbound);
	return offset_upperbound;
}

/*
 * Prerequisites : count > 0
 */

void read_shm(SPSC_queue<char>* data_buf, void* buffer, off64_t count) {
	size_t n_reads = count / *WINDOW_DATA_BUFS;
	size_t r = count % *WINDOW_DATA_BUFS;
	#ifdef CAPIOLOG
	CAPIO_DBG("read shm %ld %ld\n", n_reads, r);
	if (r)
		CAPIO_DBG("read rest shm %ld %ld\n", n_reads, r);
	#endif
	size_t i = 0;
	while (i < n_reads) {
		data_buf->read((char*) buffer + i * *WINDOW_DATA_BUFS);
		++i;
	}
	if (r)
		data_buf->read((char*) buffer + i * *WINDOW_DATA_BUFS, r);

}

#endif
