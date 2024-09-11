#ifndef CAPIO_CACHE_HPP
#define CAPIO_CACHE_HPP

class WriteRequestCache {

    int current_fd         = -1;
    long long current_size = 0;

    const capio_off64_t _max_size;

    std::filesystem::path current_path;

    // non-blocking as write is not in the pre port of CAPIO semantics
    inline void _write_request(const off64_t count, const long tid, const long fd) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, count=%ld, tid=%ld)",
                  current_path.c_str(), count, tid);
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %ld %ld %s %ld", CAPIO_REQUEST_WRITE, tid, fd, current_path.c_str(),
                count);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    }

  public:
    explicit WriteRequestCache() : _max_size(get_capio_write_cache_size()) {}

    ~WriteRequestCache() { this->flush(capio_syscall(SYS_gettid)); }

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

class ReadRequestCache {
    int current_fd         = -1;
    capio_off64_t max_read = 0;
    std::unordered_map<std::string, capio_off64_t> *available_read_cache;

    std::filesystem::path current_path;

    // return amount of readable bytes
    static inline off64_t _read_request(const std::filesystem::path &path,
                                        const off64_t end_of_Read, const long tid, const long fd) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, end_of_Read=%ld, tid=%ld, fd=%ld)",
                  path.c_str(), end_of_Read, tid, fd);
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %s %ld %ld %ld", CAPIO_REQUEST_READ, path.c_str(), tid, fd, end_of_Read);
        LOG("Sending read request %s", req);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        off64_t res;
        bufs_response->at(tid)->read(&res);
        LOG("Response to request is %ld", res);
        return res;
    }

  public:
    explicit ReadRequestCache() {
        available_read_cache = new std::unordered_map<std::string, capio_off64_t>;
    };

    ~ReadRequestCache() { delete available_read_cache; };

    void read_request(std::filesystem::path path, long end_of_read, int tid, int fd) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, end_of_read=%ld, tid=%ld)",
                  path.c_str(), end_of_read, tid);
        if (fd != current_fd || path.compare(current_path) != 0) {
            LOG("File descriptor / path changed from previous state. updating");
            current_path = std::move(path);
            current_fd   = fd;

            auto entry = available_read_cache->find(path);
            if (entry != available_read_cache->end()) {
                max_read = entry->second;
            } else {
                max_read = 0;
                available_read_cache->emplace(path, max_read);
            }
            LOG("Max read value is %llu %s", max_read, max_read == ULLONG_MAX ? "(ULLONG_MAX)" : "");
        }

        // File is committed if server reports its size to be ULLONG_MAX
        if (max_read == ULLONG_MAX) {
            LOG("Max read is ULLONG_MAX. returning as file is committed");
            return;
        }

        if (end_of_read > max_read) {
            LOG("end_of_read > max_read. Performing server request");
            max_read = _read_request(current_path, end_of_read, tid, fd);
            LOG("Obtained value from server is %ld", max_read);
            if (available_read_cache->find(path) == available_read_cache->end()) {
                available_read_cache->emplace(path, max_read);
            } else {
                available_read_cache->at(path) = max_read;
            }
            LOG("completed update from server of max read for file. returning control to "
                "application");
        }
    };
};

thread_local ReadRequestCache *read_request_cache;
thread_local WriteRequestCache *write_request_cache;

#endif // CAPIO_CACHE_HPP
