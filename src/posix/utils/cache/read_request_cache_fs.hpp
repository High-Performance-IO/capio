#ifndef READ_REQUEST_CACHE_FS_HPP
#define READ_REQUEST_CACHE_FS_HPP
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

#endif // READ_REQUEST_CACHE_FS_HPP
