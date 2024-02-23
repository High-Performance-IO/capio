#ifndef CAPIO_SERVER_UTILS_CACHE
#define CAPIO_SERVER_UTILS_CACHE

#include "capio/circular_buffer.hpp"
#include "capio/spsc_queue.hpp"

#include "requests.hpp"

inline void read_shm(SPSCQueue<char> *data_buf, void *buffer, off64_t count) {
    START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);
    size_t n_reads = count / get_caching_data_buf_size();

    LOG("read shm %ld", n_reads);
    size_t i;
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
    size_t i;
    for (i = 0; i < n_writes; i++) {
        LOG("Writing chunk of size %ld", get_caching_data_buf_size());
        data_buf->write((char *) buffer + i * get_caching_data_buf_size());
    }

    LOG("Writing remaining %ld bytes", count % get_caching_data_buf_size());
    data_buf->write((char *) buffer + i * get_caching_data_buf_size(),
                    count % get_caching_data_buf_size());
}

class ReadCache {
  private:
    std::unique_ptr<char> _cache;
    long _tid;
    size_t _cache_size, _actual_size, _cache_offset;
    SPSCQueue<char> *_queue;

    void _read_from_cache(void *buffer, size_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);

        memcpy(buffer, _cache.get() + _cache_offset, count);
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
            read_shm(_queue, _cache.get(), cached_data);
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
            read_shm(_queue, buffer, bytes_read);
            *offset = *offset + bytes_read;
        }
        return bytes_read;
    }

  public:
    ReadCache(SPSCQueue<char> *data_queue, int tid, std::size_t size)
        : _cache(new char[size]), _tid(tid), _cache_size(size), _actual_size(0),
          _cache_offset(0), _queue(data_queue) {}

    size_t read(int fd, void *buffer, off64_t *offset, size_t count) {
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

class WriteCache {
  private:
    std::unique_ptr<char> _cache;
    long _tid;
    std::size_t _cache_size;
    std::size_t _actual_size;
    SPSCQueue<char> *_queue;

    inline void send_data_to_server(int fd, std::size_t count, const void *buffer) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, count=%ld)", fd, count);

        write_request(fd, count, _tid);
        size_t n_writes = count / CAPIO_DATA_BUFFER_ELEMENT_SIZE;
        size_t r        = count % CAPIO_DATA_BUFFER_ELEMENT_SIZE;

        size_t i = 0;
        while (i < n_writes) {
            _queue->write((char *) buffer + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE);
            ++i;
        }
        if (r) {
            _queue->write((char *) buffer + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE, r);
        }
    }

  public:
    WriteCache(SPSCQueue<char> *data_queue, int tid, std::size_t size)
        : _cache(new char[size]), _tid(tid), _cache_size(size), _actual_size(0),
          _queue(data_queue) {}

    void write(int fd, const void *buffer, std::size_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, buffer=%0x%08x, count=%ld)", fd, buffer,
                  count);

        LOG("writing %ld bytes of cache", _actual_size);
        if (count > _cache_size - _actual_size) {
            // this code is the result of what used to be called flush
            if (_actual_size != 0) {
                send_data_to_server(fd, _actual_size, _cache.get());
            }
            if (count > _cache_size) {
                LOG("count %ld > _cache_size %ld\n", count, _cache.get());
                send_data_to_server(fd, count, buffer);
                return;
            }
        }
        memcpy(_cache.get() + _actual_size, buffer, count);
        _actual_size += count;
        if (_actual_size == _cache_size) {
            send_data_to_server(fd, _cache_size, _cache.get());
            _actual_size = 0;
        }
    }
};

typedef std::unordered_map<int, std::pair<WriteCache *, ReadCache *>> CPThreadDataCache_t;

#endif // CAPIO_SERVER_UTILS_CACHE
