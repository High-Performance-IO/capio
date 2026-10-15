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

class ConsentRequestCache {
    std::unordered_map<std::string, off64_t> received_consents;

  public:
    [[maybe_unused]]
    void consent_request(const std::filesystem::path &path, const long tid,
                         const std::string &source_func) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld, source=%s)", path.c_str(), tid,
                  source_func.c_str());

        const auto resolved_path = resolve_possible_symlink(path);

        if (!is_capio_path(resolved_path)) {
            LOG("PATH is forbidden. Skipping request!");
            return;
        }

        /**
         * If entry is not present in cache, then proceed to perform request. othrewise if present,
         * there is no need to perform request to server and can proceed
         */
        if (received_consents.find(resolved_path) == received_consents.end()) {
            LOG("File not present in cache. performing request");
            auto res = consent_to_proceed_request(resolved_path, tid, source_func);
            LOG("Registering new file for consent to proceed");
            received_consents.emplace(resolved_path, res);
        }
        LOG("Unlocking thread");
    }
};

class ReadCacheFS {
    int current_fd   = -1;
    off64_t max_read = 0;
    std::unordered_map<std::string, off64_t> available_read_cache;

    std::filesystem::path current_path;

  public:
    void read_request(std::filesystem::path path, const long end_of_read, int tid, const int fd) {
        START_LOG(capio_syscall(SYS_gettid), "[cache] call(path=%s, end_of_read=%ld, tid=%ld)",
                  path.c_str(), end_of_read, tid);
        if (fd != current_fd || path.compare(current_path) != 0) {
            LOG("[cache] %s changed from previous state. updating",
                fd != current_fd ? "File descriptor" : "File path");
            current_path = std::move(path);
            current_fd   = fd;

            if (const auto item = available_read_cache.find(current_path);
                item != available_read_cache.end()) {
                LOG("[cache] Found file entry in cache");
                max_read = item->second;
            } else {
                LOG("[cache] Entry not found, initializing new entry to offset 0");
                max_read = 0;
                available_read_cache.emplace(current_path, 0);
            }
            LOG("[cache] Max read value is %ld %s", max_read, max_read == -1 ? "(MAX)" : "");
        }

        // ULLONG_MAX on the wire is represented as -1 by off64_t.
        if (max_read == -1) {
            LOG("[cache] Returning as file is committed");
            return;
        }

        if (static_cast<off64_t>(end_of_read) > max_read) {
            LOG("[cache] end_of_read > max_read. Performing server request");
            max_read = read_request_fs(current_path, end_of_read, tid, fd);
            LOG("[cache] Obtained value from server is %ld", max_read);
            if (available_read_cache.find(current_path) == available_read_cache.end()) {
                LOG("[cache] Cound not find entry in cache. Adding new entry to cache");
                available_read_cache.emplace(current_path, max_read);
            } else {
                available_read_cache.at(current_path) = max_read;
                LOG("[cache] Updating max read value in cache. new value: %ld",
                    available_read_cache.at(current_path));
            }
            LOG("[cache] completed update from server of max read for file. returning control to "
                "application");
        }
    };
};

inline thread_local WriteCache *write_cache;
inline thread_local ReadCache *read_cache;
inline thread_local ConsentRequestCache *consent_request_cache;
inline thread_local ReadCacheFS *read_request_cache;

/**
 * Add a new response buffer for thread @param tid
 * @param tid
 * @return
 */
inline void initialize_data_queues(const long tid) {
    write_cache =
        new WriteCache(tid, get_cache_lines(), get_cache_line_size(), get_capio_workflow_name());
    read_cache =
        new ReadCache(tid, get_cache_lines(), get_cache_line_size(), get_capio_workflow_name());
    consent_request_cache = new ConsentRequestCache();
    read_request_cache    = new ReadCacheFS();
}

#endif // CAPIO_SERVER_UTILS_CACHE
