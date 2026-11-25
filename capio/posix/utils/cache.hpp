#ifndef CAPIO_SERVER_UTILS_CACHE
#define CAPIO_SERVER_UTILS_CACHE

#include "common/dirent.hpp"
#include "common/queue.hpp"

#include "requests.hpp"

#include <fcntl.h>
#include <sys/stat.h>

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
            seek_request(_last_fd, fd_table->offset(_last_fd), _tid);
        }
    }

    // Check whether this instance caches data from the file path associated with the given
    // fd or not
    [[nodiscard]] bool cachesFileOf(const int fd) const {
        return fd == _last_fd ||
               (fd_table->contains(_last_fd) && fd_table->path(fd) == fd_table->path(_last_fd));
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
        off64_t file_offset     = fd_table->offset(fd);
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

            // NOTE: if getdents send a request for exactly the correct amount of data.
            if (read_size > _max_line_size || is_getdents) {
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
        fd_table->setOffset(fd, file_offset + bytes_read);
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

        fd_table->setOffset(fd, fd_table->offset(fd) + count);
    }
};

/// @brief FS variant: block until the server lets a call on a path go on, as stat waits in the
/// memory variant
class ConsentRequestCache {
    std::unordered_set<std::string> received_consents;

  public:
    void consent_request(const std::filesystem::path &path, const long tid,
                         const std::string &source_func) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld, source=%s)", path.c_str(), tid,
                  source_func.c_str());

        if (received_consents.find(path) != received_consents.end()) {
            return;
        }
        // 0 holds only now: the path may be created or committed later
        if (consent_to_proceed_request(path, tid, source_func) != 0) {
#ifdef SYS_statx
            // NFS may still have the size from before the wait cached
            struct statx fresh{};
            capio_syscall(SYS_statx, AT_FDCWD, path.c_str(), AT_STATX_FORCE_SYNC, STATX_SIZE,
                          &fresh);
#endif
            received_consents.emplace(path);
        }
    }
};

/// @brief FS variant: block a read until the server says its bytes are on the file system
class ReadCacheFS {
    // path -> bytes known to be readable, -1 for all of them: committed, or nothing to wait for
    std::unordered_map<std::string, off64_t> available_read_cache;

  public:
    /// @brief Block until the first @p end_of_read bytes of the file of @p fd can be read
    void read(const int fd, const off64_t end_of_read, const long tid) {
        const std::string &path = fd_table->path(fd);
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, end_of_read=%ld, tid=%ld)",
                  path.c_str(), end_of_read, tid);

        const auto known = available_read_cache.find(path);
        if (known != available_read_cache.end() &&
            (known->second == -1 || end_of_read <= known->second)) {
            return;
        }
        available_read_cache[path] = read_request_fs(path, end_of_read, tid, fd);
#ifdef SYS_statx
        // NFS may still have the old size cached, and would answer end of file
        struct statx fresh{};
        capio_syscall(SYS_statx, fd, "", AT_EMPTY_PATH | AT_STATX_FORCE_SYNC, STATX_SIZE, &fresh);
#endif
    }
};

inline thread_local WriteCache *write_cache;
inline thread_local ReadCache *read_cache;
inline thread_local ConsentRequestCache *consent_request_cache;
inline thread_local ReadCacheFS *read_request_cache;

#endif // CAPIO_SERVER_UTILS_CACHE
