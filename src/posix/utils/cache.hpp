#ifndef CAPIO_CACHE_HPP
#define CAPIO_CACHE_HPP
#include "capio/requests.hpp"
#include "env.hpp"

class WriteRequestCacheFS {

    int current_fd         = -1;
    long long current_size = 0;

    const capio_off64_t _max_size;

    std::filesystem::path current_path;

    // non-blocking as write is not in the pre port of CAPIO semantics
    inline void _write_request(const off64_t count, const long tid, const long fd) const {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, count=%ld, tid=%ld)",
                  current_path.c_str(), count, tid);
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %ld %ld %s %ld", CAPIO_REQUEST_WRITE, tid, fd, current_path.c_str(),
                count);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    }

  public:
    explicit WriteRequestCacheFS() : _max_size(get_capio_write_cache_size()) {}

    ~WriteRequestCacheFS() { this->flush(capio_syscall(SYS_gettid)); }

    void write_request(std::filesystem::path path, int tid, int fd, long count) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld, fd=%ld, count=%ld)",
                  path.c_str(), tid, fd, count);
        if (fd != current_fd || path.compare(current_path) != 0) {
            LOG("File descriptor changed from previous state. updating");
            this->flush(tid);
            current_path = std::move(path);
            current_fd   = fd;
        }
        current_size += count;

        if (current_size > _max_size) {
            LOG("exceeded maximum cache size. flushing...");
            this->flush(tid);
        }
    };

    void flush(int tid) {
        START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld)", tid);
        if (current_fd != -1 && current_size > 0) {
            LOG("Performing write to SHM");
            _write_request(tid, current_fd, current_size);
        }
        current_fd   = -1;
        current_size = 0;
    }
};

class ReadRequestCacheFS {
    int current_fd         = -1;
    capio_off64_t max_read = 0;
    std::unordered_map<std::string, capio_off64_t> *available_read_cache;

    std::filesystem::path current_path;

    // return amount of readable bytes
    static capio_off64_t _read_request(const std::filesystem::path &path, const off64_t end_of_Read,
                                       const long tid, const long fd) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, end_of_Read=%ld, tid=%ld, fd=%ld)",
                  path.c_str(), end_of_Read, tid, fd);
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %ld %ld %s %ld", CAPIO_REQUEST_READ, tid, fd, path.c_str(), end_of_Read);
        LOG("Sending read request %s", req);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        capio_off64_t res;
        bufs_response->at(tid)->read(&res);
        LOG("Response to request is %llu", res);
        return res;
    }

  public:
    explicit ReadRequestCacheFS() {
        available_read_cache = new std::unordered_map<std::string, capio_off64_t>;
    };

    ~ReadRequestCacheFS() { delete available_read_cache; };

    void read_request(std::filesystem::path path, long end_of_read, int tid, int fd) {
        START_LOG(capio_syscall(SYS_gettid), "[cache] call(path=%s, end_of_read=%ld, tid=%ld)",
                  path.c_str(), end_of_read, tid);
        if (fd != current_fd || path.compare(current_path) != 0) {
            LOG("[cache] %s changed from previous state. updating",
                fd != current_fd ? "File descriptor" : "File path");
            current_path = std::move(path);
            current_fd   = fd;

            auto item = available_read_cache->find(current_path);
            if (item != available_read_cache->end()) {
                LOG("[cache] Found file entry in cache");
                max_read = item->second;
            } else {
                LOG("[cache] Entry not found, initializing new entry to offset 0");
                max_read = 0;
                available_read_cache->emplace(current_path, max_read);
            }
            LOG("[cache] Max read value is %llu %s", max_read,
                max_read == ULLONG_MAX ? "(ULLONG_MAX)" : "");
        }

        // File is committed if server reports its size to be ULLONG_MAX
        if (max_read == ULLONG_MAX) {
            LOG("[cache] Returning as file is committed");
            return;
        }

        if (end_of_read > max_read) {
            LOG("[cache] end_of_read > max_read. Performing server request");
            max_read = _read_request(current_path, end_of_read, tid, fd);
            LOG("[cache] Obtained value from server is %llu", max_read);
            if (available_read_cache->find(path) == available_read_cache->end()) {
                available_read_cache->emplace(path, max_read);
            } else {
                available_read_cache->at(path) = max_read;
            }
            LOG("[cache] completed update from server of max read for file. returning control to "
                "application");
        }
    };
};

class ConsentRequestCache {

    std::unordered_map<std::string, capio_off64_t> *available_consent;

    // Block until server allows for proceeding to a generic request
    static inline capio_off64_t _consent_to_proceed_request(const std::filesystem::path &path,
                                                            const long tid,
                                                            const std::string &source_func) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld, source_func=%s)", path.c_str(),
                  tid, source_func.c_str());
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %ld %s %s", CAPIO_REQUEST_CONSENT, tid, path.c_str(),
                source_func.c_str());
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        capio_off64_t res;
        bufs_response->at(tid)->read(&res);
        LOG("Obtained from server %llu", res);
        return res;
    }

  public:
    explicit ConsentRequestCache() {
        available_consent = new std::unordered_map<std::string, capio_off64_t>;
    };

    ~ConsentRequestCache() { delete available_consent; };

    void consent_request(const std::filesystem::path &path, long tid,
                         const std::string &source_func) const {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld, source=%s)", path.c_str(), tid,
                  source_func.c_str());

        /**
         * If entry is not present in cache, then proceed to perform request. othrewise if present,
         * there is no need to perform request to server and can proceed
         */
        if (available_consent->find(path) == available_consent->end()) {
            LOG("File not present in cache. performing request");
            auto res = ConsentRequestCache::_consent_to_proceed_request(path, tid, source_func);
            LOG("Registering new file for consent to proceed");
            available_consent->emplace(path, res);
        }
        LOG("Unlocking thread");
    }
};

class ReadRequestCacheMEM {
  private:
    char *_cache;
    long _tid;
    int _last_fd;
    capio_off64_t _max_line_size, _actual_size, _cache_offset;

    void _read(void *buffer, capio_off64_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);

        if (count > 0) {
            memcpy(buffer, _cache + _cache_offset, count);
            LOG("Read %ld. adding it to _cache_offset of value %ld", count, _cache_offset);
            _cache_offset += count;
        }
    }

    static capio_off64_t seek_request(const int fd, const capio_off64_t offset, const long tid) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, offset=%ld, tid=%ld)", fd, offset, tid);
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %ld %d %llu", -1, tid, fd, offset);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        capio_off64_t res;
        bufs_response->at(tid)->read(&res);
        return res;
    }

    static capio_off64_t read_request(const int fd, const capio_off64_t count, const long tid) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, count=%ld, tid=%ld)", fd, count, tid);
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %ld %d %llu", CAPIO_REQUEST_READ_MEM, tid, fd, count);
        LOG("Sending read request %s", req);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        capio_off64_t res;
        bufs_response->at(tid)->read(&res);
        LOG("Response to request is %ld", res);
        return res;
    }

  public:
    explicit ReadRequestCacheMEM(const long lines                 = get_cache_lines(),
                                 const long line_size             = get_cache_line_size(),
                                 const std::string &workflow_name = get_capio_workflow_name())
        : _cache(nullptr), _tid(capio_syscall(SYS_gettid)), _last_fd(-1), _max_line_size(line_size),
          _actual_size(0), _cache_offset(0) {}

    inline void flush() {
        START_LOG(capio_syscall(SYS_gettid), "call()");

        if (_cache_offset != _actual_size) {
            _actual_size = _cache_offset = 0;
            seek_request(_last_fd, get_capio_fd_offset(_last_fd), _tid);
        }
    }

    inline capio_off64_t read(const int fd, void *buffer, off64_t count, bool is64bit) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, count=%ld, is_getdents=%s, is64bit=%s)",
                  fd, count, is64bit ? "true" : "false");

        if (_last_fd != fd) {
            LOG("changed fd from %d to %d: flushing", _last_fd, fd);
            flush();
            _last_fd = fd;
        }

        const capio_off64_t remaining_bytes = _actual_size - _cache_offset;
        const capio_off64_t file_offset     = get_capio_fd_offset(fd);
        capio_off64_t bytes_read;

        auto read_size = count - remaining_bytes;
        LOG("Read() will need to read %ld bytes", read_size);

        if (count <= remaining_bytes) {
            LOG("count %ld <= remaining_bytes %ld", count, remaining_bytes);
            _read(buffer, count);
            bytes_read = count;
        } else {
            LOG("count %ld > remaining_bytes %llu", count, remaining_bytes);
            _read(buffer, remaining_bytes);
            buffer = static_cast<char *>(buffer) + remaining_bytes;

            if (read_size > _max_line_size) {
                LOG("count - remaining_bytes %ld > _max_line_size %ld", read_size, _max_line_size);
                LOG("Reading exactly requested size");
                const capio_off64_t end_of_read = read_request(fd, read_size, _tid);
                bytes_read                      = end_of_read - file_offset;
                stc_queue->read(static_cast<char *>(buffer), bytes_read);
            } else {
                LOG("count - remaining_bytes %ld <= _max_line_size %ld", read_size, _max_line_size);
                LOG("Reading more to use pre fetching and caching");
                const capio_off64_t end_of_read = read_request(fd, _max_line_size, _tid);
                LOG("request return value is %ld", end_of_read);
                _actual_size = end_of_read - file_offset - remaining_bytes;
                LOG("ReaderCache actual size, after requested read is: %ld bytes", _actual_size);
                _cache_offset = 0;
                if (_actual_size > 0) {
                    LOG("Fetching data from shm _queue");
                    _cache = stc_queue->fetch();
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

class WriteRequestCacheMEM {
    char *_cache;
    long _tid;
    int _fd;
    off64_t _max_line_size, _actual_size;

    void _write(const off64_t count, const void *buffer) {
        START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);

        if (count > 0) {
            if (_cache == nullptr) {
                _cache = cts_queue->reserve();
            }
            memcpy(_cache + _actual_size, buffer, count);
            _actual_size += count;
            if (_actual_size == _max_line_size) {
                flush();
            }
        }
    }

    void write_request(const off64_t count, const long tid, const long fd) const {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, count=%ld, tid=%ld)",
                  get_capio_fd_path(_fd).c_str(), count, tid);
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %ld %ld %s %ld", CAPIO_REQUEST_WRITE_MEM, tid, fd,
                get_capio_fd_path(_fd).c_str(), count);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    }

  public:
    explicit WriteRequestCacheMEM(off64_t line_size = get_cache_line_size())
        : _cache(nullptr), _tid(capio_syscall(SYS_gettid)), _fd(-1), _max_line_size(line_size),
          _actual_size(0) {}

    void flush() {
        START_LOG(capio_syscall(SYS_gettid), "call()");

        if (_actual_size != 0) {
            write_request(_fd, _actual_size, _tid);
            _cache       = nullptr;
            _actual_size = 0;
        }
    }

    void write(int fd, const void *buffer, off64_t count) {
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
                cts_queue->write(static_cast<const char *>(buffer), count);
            } else {
                LOG("count - _actual_size %ld <= _max_line_size %ld", count - _actual_size,
                    _max_line_size);
                _write(count, buffer);
            }
        }

        set_capio_fd_offset(fd, get_capio_fd_offset(fd) + count);
    }
};

inline thread_local ConsentRequestCache *consent_request_cache_fs;
inline thread_local ReadRequestCacheFS *read_request_cache_fs;
inline thread_local WriteRequestCacheFS *write_request_cache_fs;
inline thread_local WriteRequestCacheMEM *write_request_cache_mem;
inline thread_local ReadRequestCacheMEM *read_request_cache_mem;

inline void init_caches() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    write_request_cache_fs   = new WriteRequestCacheFS();
    read_request_cache_fs    = new ReadRequestCacheFS();
    consent_request_cache_fs = new ConsentRequestCache();
    write_request_cache_mem  = new WriteRequestCacheMEM();
    read_request_cache_mem   = new ReadRequestCacheMEM();
}

inline void delete_caches() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    delete write_request_cache_fs;
    delete read_request_cache_fs;
    delete consent_request_cache_fs;
    delete write_request_cache_mem;
    delete read_request_cache_mem;
}

#endif // CAPIO_CACHE_HPP
