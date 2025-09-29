#ifndef CAPIO_SERVER_UTILS_CACHE
#define CAPIO_SERVER_UTILS_CACHE

#include "common/dirent.hpp"
#include "common/queue.hpp"

#include "requests.hpp"

class ReadCache {
  private:
    char *_cache;
    long _tid;
    int _last_fd;
    off64_t _max_line_size, _actual_size, _cache_offset;
    SPSCQueue _queue;

    inline void _read(void *buffer, off64_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);

        if (count > 0) {
            memcpy(buffer, _cache + _cache_offset, count);
            LOG("Read %ld. adding it to _cache_offset of value %ld", count, _cache_offset);
            _cache_offset += count;
        }
    }

  public:
    ReadCache(long tid, off64_t lines, off64_t line_size, const std::string &workflow_name)
        : _cache(nullptr), _tid(tid), _last_fd(-1), _max_line_size(line_size), _actual_size(0),
          _cache_offset(0),
          _queue(SHM_SPSC_PREFIX_READ + std::to_string(tid), lines, line_size, workflow_name) {}

    inline void flush() {
        START_LOG(capio_syscall(SYS_gettid), "call()");

        if (_cache_offset != _actual_size) {
            _actual_size = _cache_offset = 0;
            seek_request(_last_fd, get_capio_fd_offset(_last_fd), _tid);
        }
    }

    inline off64_t read(int fd, void *buffer, off64_t count, bool is_getdents, bool is64bit) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, count=%ld, is_getdents=%s, is64bit=%s)",
                  fd, count, is_getdents ? "true" : "false", is64bit ? "true" : "false");

        if (_last_fd != fd) {
            LOG("changed fd from %d to %d: flushing", _last_fd, fd);
            flush();
            _last_fd = fd;
        }

        off64_t remaining_bytes = _actual_size - _cache_offset;
        off64_t file_offset     = get_capio_fd_offset(fd);
        off64_t bytes_read;

        if (is_getdents) {
            auto dirent_size = static_cast<off64_t>(sizeof(linux_dirent64));
            count            = (count / dirent_size) * dirent_size;
        }

        auto read_size = count - remaining_bytes;
        LOG("Read() will need to read %ld bytes", read_size);

        if (count <= remaining_bytes) {
            LOG("count %ld <= remaining_bytes %ld", count, remaining_bytes);
            _read(buffer, count);
            bytes_read = count;
        } else {
            LOG("count %ld > remaining_bytes %ld", count, remaining_bytes);
            _read(buffer, remaining_bytes);
            buffer = reinterpret_cast<char *>(buffer) + remaining_bytes;

            if (read_size > _max_line_size) {
                LOG("count - remaining_bytes %ld > _max_line_size %ld", read_size, _max_line_size);
                LOG("Reading exactly requested size");
                off64_t end_of_read = is_getdents ? getdents_request(fd, read_size, is64bit, _tid)
                                                  : read_request(fd, read_size, _tid);
                bytes_read          = end_of_read - file_offset;
                _queue.read(reinterpret_cast<char *>(buffer), bytes_read);
            } else {
                LOG("count - remaining_bytes %ld <= _max_line_size %ld", read_size, _max_line_size);
                LOG("Reading more to use pre fetching and caching");
                off64_t end_of_read = is_getdents
                                          ? getdents_request(fd, _max_line_size, is64bit, _tid)
                                          : read_request(fd, _max_line_size, _tid);
                LOG("request return value is %ld", end_of_read);
                _actual_size = end_of_read - file_offset - remaining_bytes;
                LOG("ReaderCache actual size, after requested read is: %ld bytes", _actual_size);
                _cache_offset = 0;
                if (_actual_size > 0) {
                    LOG("Fetching data from shm _queue");
                    _cache = _queue.fetch();
                }
                if (read_size < _actual_size) {
                    LOG("count - remaining_bytes %ld < _actual_size %ld", read_size, _actual_size);
                    _read(buffer, read_size);
                    bytes_read = count;
                } else {
                    LOG("count - remaining_bytes %ld >= _actual_size %ld", read_size, _actual_size);
                    _read(buffer, _actual_size);
                    bytes_read = remaining_bytes + _actual_size;
                }
            }
        }
        LOG("%ld bytes have been read. setting fd offset to %ld", bytes_read,
            file_offset + bytes_read);
        set_capio_fd_offset(fd, file_offset + bytes_read);
        return bytes_read;
    }
};

class WriteCache {
  private:
    char *_cache;
    long _tid;
    int _fd;
    off64_t _max_line_size, _actual_size;
    SPSCQueue _queue;

    inline void _write(off64_t count, const void *buffer) {
        START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);

        if (count > 0) {
            if (_cache == nullptr) {
                _cache = _queue.reserve();
            }
            memcpy(_cache + _actual_size, buffer, count);
            _actual_size += count;
            if (_actual_size == _max_line_size) {
                flush();
            }
        }
    }

  public:
    WriteCache(long tid, off64_t lines, off64_t line_size, const std::string &workflow_name)
        : _cache(nullptr), _tid(tid), _fd(-1), _max_line_size(line_size), _actual_size(0),
          _queue(SHM_SPSC_PREFIX_WRITE + std::to_string(tid), lines, line_size, workflow_name) {}

    inline void flush() {
        START_LOG(capio_syscall(SYS_gettid), "call()");

        if (_actual_size != 0) {
            write_request(_fd, _actual_size, _tid);
            _cache       = nullptr;
            _actual_size = 0;
        }
    }

    inline void write(int fd, const void *buffer, off64_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, buffer=0x%08x, count=%ld)", fd, buffer,
                  count);

        if (_fd != fd) {
            LOG("changed fd from %d to %d: flushing", _fd, fd);
            flush();
            _fd = fd;
        }

        if (count <= _max_line_size - _actual_size) {
            LOG("count %ld <= _max_line_size - _actual_size %ld", count,
                _max_line_size - _actual_size);
            _write(count, buffer);
        } else {
            LOG("count %ld > _max_line_size - _actual_size %ld", count,
                _max_line_size - _actual_size);
            flush();
            if (count - _actual_size > _max_line_size) {
                LOG("count - _actual_size %ld > _max_line_size %ld", count - _actual_size,
                    _max_line_size);
                write_request(_fd, count, _tid);
                _queue.write(reinterpret_cast<const char *>(buffer), count);
            } else {
                LOG("count - _actual_size %ld <= _max_line_size %ld", count - _actual_size,
                    _max_line_size);
                _write(count, buffer);
            }
        }

        set_capio_fd_offset(fd, get_capio_fd_offset(fd) + count);
    }
};

typedef std::unordered_map<long, std::pair<WriteCache *, ReadCache *>> CPThreadDataCache_t;

#endif // CAPIO_SERVER_UTILS_CACHE
