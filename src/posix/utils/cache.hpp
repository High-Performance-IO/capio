#ifndef CAPIO_SERVER_UTILS_CACHE
#define CAPIO_SERVER_UTILS_CACHE

#include "capio/circular_buffer.hpp"
#include "capio/spsc_queue.hpp"
#include "requests.hpp"

inline void read_shm(SPSCQueue<char> *data_buf, void *buffer, off64_t count) {
    START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);
    size_t n_reads = count / get_caching_data_buf_size();

    LOG("read shm %ld", n_reads);
    size_t i = 0;
    for (i = 0; i < n_reads; i++) {
        LOG("Reading chunk of size %ld of data", get_caching_data_buf_size());
        data_buf->read((char *) buffer + i * get_caching_data_buf_size());
    }

    LOG("Reading remaining %ld bytes of data", count % get_caching_data_buf_size());
    data_buf->read((char *) buffer + i * get_caching_data_buf_size(),
                   count % get_caching_data_buf_size());
}

inline void write_shm(SPSCQueue<char> *data_buf, const void *buffer, off64_t count) {
    START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);
    size_t n_writes = count / get_caching_data_buf_size();

    LOG("write shm %ld", n_writes);
    size_t i = 0;
    for (i = 0; i < n_writes; i++) {
        LOG("Writing chunk of size %ld", get_caching_data_buf_size());
        data_buf->write((char *) buffer + i * get_caching_data_buf_size());
    }

    LOG("Writing remaining %ld bytes", count % get_caching_data_buf_size());
    data_buf->write((char *) buffer + i * get_caching_data_buf_size(),
                    count % get_caching_data_buf_size());
}

class ReaderCache {
  private:
    char *_cache;
    size_t _cache_size, _actual_size, _cache_offset;
    SPSCQueue<char> *_shm_data_queue;
    int _tid;

    void _read_from_cache(void *buffer, size_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);

        memcpy(buffer, _cache + _cache_offset, count);
        _cache_offset += count;
    }

    off64_t populate_cache(int fd, void *buffer, off64_t *offset, size_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        off64_t end_of_read, cached_data, bytes_read;
        end_of_read = read_request(_tid, fd, _cache_size);
        LOG("populate caching\n");
        if (end_of_read == 0) {
            return 0;
        }
        bytes_read  = end_of_read - *offset;
        cached_data = bytes_read;
        if (bytes_read > count) {
            bytes_read = count;
        }

        if (cached_data > 0) {
            read_shm(_shm_data_queue, _cache, cached_data);
            _read_from_cache(buffer, count);
            _actual_size += cached_data;
            if (_cache_offset == _cache_size) {
                _cache_offset = 0;
                _actual_size  = 0;
            }

            LOG("after read shm\n");
            *offset = *offset + cached_data;
        }

        return bytes_read;
    }

    off64_t query_data_from_server(int fd, void *buffer, off64_t *offset, size_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        off64_t end_of_read, bytes_read;
        end_of_read = read_request(_tid, fd, count);
        if (end_of_read == 0) {
            return 0;
        }
        bytes_read = end_of_read - *offset;

        LOG("before read shm bytes_read %ld end_of_read %ld offset %ld\n", bytes_read, end_of_read,
            *offset);
        if (bytes_read > 0) {
            read_shm(_shm_data_queue, buffer, bytes_read);
            *offset = *offset + bytes_read;
        }
        return bytes_read;
    }

  public:
    ReaderCache(int tid, std::size_t cache_size, SPSCQueue<char> *data_queue)
        : _cache_size(cache_size), _actual_size(0), _cache_offset(0), _shm_data_queue(data_queue),
          _tid(tid) {
        _cache = new char[cache_size];
    }

    ~ReaderCache() { delete[] _cache; }

    size_t read_from_server(int fd, void *buffer, off64_t *offset, size_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, offset=%ld, count=%ld)", fd, offset,
                  count);
        size_t remnant_bytes = _actual_size - _cache_offset;
        if (count <= remnant_bytes) {
            LOG("reading from cache offset %ld actual size %ld\n", _cache_offset, _actual_size);

            _read_from_cache(buffer, count);

            LOG("after reading from cache\n");

            if (_cache_offset == _cache_size) {
                _cache_offset = 0;
                _actual_size  = 0;
            }
            return count;
        }

        size_t bytes_read = 0;
        if (remnant_bytes != 0) {
            _read_from_cache(buffer, remnant_bytes);
            count -= remnant_bytes;
            _cache_offset = 0;
            _actual_size  = 0;
            bytes_read += remnant_bytes;
        }

        if (count > _cache_size) {
            bytes_read += query_data_from_server(fd, buffer, offset, count);
        } else {
            bytes_read += populate_cache(fd, buffer, offset, count);
        }

        LOG("capio_read returning  %ld\n", bytes_read);

        return bytes_read;
    }
};

class WriterCache {
  private:
    char *_cache;
    std::size_t _cache_size;
    std::size_t _actual_size;
    SPSCQueue<char> *_shm_data_queue;

    inline void send_data_to_server(int fd, off64_t *offset, std::size_t count,
                                    const void *buffer) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, offset=%ld, count=%ld)", fd, offset,
                  count);
        LOG("sending data to server");
        long int old_offset = *offset;
        *offset += count; // works only if there is only one writer at time for each file
        int tid = capio_syscall(SYS_gettid);
        cached_write_request(tid, fd, count, old_offset);
        write_shm(_shm_data_queue, buffer, count);
    }

  public:
    WriterCache(std::size_t cache_size, SPSCQueue<char> *data_queue)
        : _cache_size(cache_size), _actual_size(0), _shm_data_queue(data_queue) {
        _cache = new char[cache_size];
    }

    ~WriterCache() { delete[] _cache; }

    void write(int fd, off64_t *offset, const void *buffer, std::size_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, offset=%ld, count=%ld)", fd, offset,
                  count);
        LOG("writing %ld bytes of cache", _actual_size);
        if (count > _cache_size - _actual_size) {
            // this code is the result of what used to be called flush
            if (_actual_size != 0) {
                send_data_to_server(fd, offset, _actual_size, _cache);
            }
            if (count > _cache_size) {
                LOG("count %ld > _cache_size %ld\n", count, _cache);
                send_data_to_server(fd, offset, count, buffer);
                return;
            }
        }
        memcpy((char *) _cache + _actual_size, buffer, count);
        _actual_size += count;
        if (_actual_size == _cache_size) {
            send_data_to_server(fd, offset, _cache_size, _cache);
            _actual_size = 0;
        }
    }
};
#endif // CAPIO_SERVER_UTILS_CACHE
