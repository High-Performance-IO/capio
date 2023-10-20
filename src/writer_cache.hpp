#ifndef CAPIO_WRITER_CACHE_HPP_
#define CAPIO_WRITER_CACHE_HPP_

#include "utils/client_utils.hpp"

class Writer_cache {
	private:
		void* _cache;
		std::size_t _cache_size;
		std::size_t _actual_size;
		SPSC_queue<char>* _shm_data_queue;
		Circular_buffer<char>* _buf_reqs;

		void send_data_to_server(int fd, off64_t* offset, std::size_t count, const void* buffer) {
			#ifdef CAPIOLOG
			CAPIO_DBG("sending data to server, size %ld\n", count);
			#endif
			long int old_offset = *offset;
			*offset += count; //works only if there is only one writer at time for each file
			int tid = syscall_no_intercept(SYS_gettid);
			add_write_request(tid, fd, count, old_offset, _buf_reqs);
			write_shm(_shm_data_queue, buffer, count);
		}

	public:

		/* Constructor */

		Writer_cache(std::size_t cache_size, SPSC_queue<char>* data_queue, Circular_buffer<char>* buf_reqs) {
			_cache_size = cache_size;
			_cache = malloc(sizeof(char) * cache_size);
			_shm_data_queue = data_queue;
			_buf_reqs = buf_reqs;
			_actual_size = 0;
		}

		/* Destructor */

		~Writer_cache() {
			free(_cache);
		}

		void write_to_server(int fd, off64_t* offset, const void* buffer, std::size_t count) {
			#ifdef CAPIOLOG
			CAPIO_DBG("writing cache %ld\n", _actual_size);
			#endif
			if (count > _cache_size - _actual_size) {
				flush(fd, offset);
				if (count > _cache_size) {
					#ifdef CAPIOLOG
					CAPIO_DBG("count %ld > _cache_size %ld\n", count, _cache);
					#endif
					send_data_to_server(fd, offset, count, buffer);
					return;
				}
			}
			memcpy((char*) _cache + _actual_size, buffer, count);
			_actual_size += count;
			if (_actual_size == _cache_size) {
				send_data_to_server(fd, offset, _cache_size, _cache);
				_actual_size = 0;
			}		
		}

		void flush(int fd, off64_t* offset) {
			if (_actual_size == 0)
				return;
			#ifdef CAPIOLOG
			CAPIO_DBG("Flushing cache %ld\n", _actual_size);
			#endif
			send_data_to_server(fd, offset, _actual_size, _cache);
			_actual_size = 0;
		}
};

#endif
