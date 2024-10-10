#ifndef CAPIO_CACHE_HPP
#define CAPIO_CACHE_HPP

class WriteRequestCache {

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
    static capio_off64_t _read_request(const std::filesystem::path &path, const off64_t end_of_Read,
                                       const long tid, const long fd) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, end_of_Read=%ld, tid=%ld, fd=%ld)",
                  path.c_str(), end_of_Read, tid, fd);
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %s %ld %ld %ld", CAPIO_REQUEST_READ, path.c_str(), tid, fd, end_of_Read);
        LOG("Sending read request %s", req);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        capio_off64_t res;
        bufs_response->at(tid)->read(&res);
        LOG("Response to request is %llu", res);
        return res;
    }

  public:
    explicit ReadRequestCache() {
        available_read_cache = new std::unordered_map<std::string, capio_off64_t>;
    };

    ~ReadRequestCache() { delete available_read_cache; };

    void read_request(std::filesystem::path path, long end_of_read, int tid, int fd) {
        START_LOG(capio_syscall(SYS_gettid), "[cache] call(path=%s, end_of_read=%ld, tid=%ld)",
                  path.c_str(), end_of_read, tid);
        if (fd != current_fd || path.compare(current_path) != 0) {
            LOG("[cache] %s changed from previous state. updating",
                fd != current_fd ? "File descriptor" : "File path");
            current_path = std::move(path);
            current_fd   = fd;

            if (available_read_cache->find(current_path) != available_read_cache->end()) {
                LOG("[cache] Found file entry in cache");
                max_read = available_read_cache->at(current_path);
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
            LOG("Registering new file for consent to proceed");
            available_consent->emplace(path, _consent_to_proceed_request(path, tid, source_func));
        }
        LOG("Unlocking thread");
    }
};

inline thread_local ConsentRequestCache *consent_request_cache;
inline thread_local ReadRequestCache *read_request_cache;
inline thread_local WriteRequestCache *write_request_cache;

inline void init_caches() {
    write_request_cache   = new WriteRequestCache();
    read_request_cache    = new ReadRequestCache();
    consent_request_cache = new ConsentRequestCache();
}

inline void delete_caches() {
    delete write_request_cache;
    delete read_request_cache;
    delete consent_request_cache;
}

#endif // CAPIO_CACHE_HPP
