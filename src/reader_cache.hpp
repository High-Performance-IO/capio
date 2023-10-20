#ifndef CAPIO_READER_CACHE_HPP_
#define CAPIO_READER_CACHE_HPP_

#include "utils/client_utils.hpp"

class Reader_cache {
	private:
		char* _cache;
		size_t _cache_size;
		size_t _actual_size;
		size_t _cache_offset;
		SPSC_queue<char>* _shm_data_queue;
		Circular_buffer<char>* _buf_reqs;
		Circular_buffer<off_t>* _buf_response;
		int _tid;

		void _read_from_cache(void* buffer, size_t count) {
			#ifdef CAPIOLOG
			CAPIO_DBG("reading from real cache count %ld _cache_offset %ld\n", count, _cache_offset);
			#endif
			memcpy(buffer, _cache + _cache_offset, count);
			_cache_offset += count;
		}

		off64_t populate_cache(int fd, void* buffer, off64_t* offset, size_t count) {
			off64_t end_of_read, cached_data, bytes_read;
			end_of_read = add_read_request(_tid, fd, _cache_size, _buf_reqs, _buf_response);
			#ifdef CAPIOLOG
			CAPIO_DBG("populate caching\n");
			#endif
			if (end_of_read == 0)
		  		return 0;
			bytes_read = end_of_read - *offset;
			cached_data = bytes_read;
			if (bytes_read > count)
				bytes_read = count;
			#ifdef CAPIOLOG
			CAPIO_DBG("before read shm bytes_read %ld end_of_read %ld cached_data %ld offset %ld\n", bytes_read, end_of_read, cached_data, *offset);
			#endif
	
			if (cached_data > 0) {
				read_shm(_shm_data_queue, _cache, cached_data);
				_read_from_cache(buffer, count);
				_actual_size += cached_data;
				if (_cache_offset == _cache_size) {
					_cache_offset = 0;
					_actual_size= 0;
				}
				#ifdef CAPIOLOG
				CAPIO_DBG("after read shm\n");
				#endif
				*offset = *offset + cached_data;
			}
	
			return bytes_read;
		}

		off64_t query_data_from_server(int fd, void* buffer, off64_t* offset, size_t count) {
			off64_t end_of_read, bytes_read;
			end_of_read = add_read_request(_tid, fd, count, _buf_reqs, _buf_response);
			if (end_of_read == 0)
		  		return 0;
			bytes_read = end_of_read - *offset;
			#ifdef CAPIOLOG
			CAPIO_DBG("before read shm bytes_read %ld end_of_read %ld offset %ld\n", bytes_read, end_of_read, *offset);
			#endif
			if (bytes_read > 0) {
				read_shm(_shm_data_queue, buffer, bytes_read);
				*offset = *offset + bytes_read;
			}
			return bytes_read;
		}

	public:

		/* Constructor */

		Reader_cache(int tid, std::size_t cache_size, SPSC_queue<char>* data_queue, Circular_buffer<char>* buf_reqs, Circular_buffer<off_t>* buf_response) {
			_cache_size = cache_size;
			_cache = (char*) malloc(sizeof(char) * cache_size);
			_shm_data_queue = data_queue;
			_buf_reqs = buf_reqs;
			_buf_response = buf_response;
			_actual_size = 0;
			_cache_offset = 0;
			_tid = tid;
		}

		/* Destructor */

		~Reader_cache() {
			free(_cache);
		}

		size_t read_from_server(int fd, void* buffer, off64_t* offset, size_t count) {
			#ifdef CAPIOLOG
			CAPIO_DBG("read from cache\n");
			#endif
			size_t remnant_bytes = _actual_size - _cache_offset;
			if (count <= remnant_bytes) {
				#ifdef CAPIOLOG
				CAPIO_DBG("reading from cache offset %ld actual size %ld\n", _cache_offset, _actual_size);
				#endif
				_read_from_cache(buffer, count);
				#ifdef CAPIOLOG
				CAPIO_DBG("after reading from cache\n");
				#endif
				if (_cache_offset == _cache_size) {
					_cache_offset = 0;
					_actual_size= 0;
				}
				return count;
			}

			size_t bytes_read = 0;
			if (remnant_bytes != 0) {
				_read_from_cache(buffer, remnant_bytes);
				count -= remnant_bytes;
				_cache_offset = 0;
				_actual_size = 0;
				bytes_read += remnant_bytes;
			}

			if (count > _cache_size)
				bytes_read += query_data_from_server(fd, buffer, offset, count);
			else
				bytes_read += populate_cache(fd, buffer, offset, count);
			#ifdef CAPIOLOG
			CAPIO_DBG("capio_read returning  %ld\n", bytes_read);
			#endif
			return bytes_read;
		}
};

#endif
