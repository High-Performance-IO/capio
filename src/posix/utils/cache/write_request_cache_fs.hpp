#ifndef WRITE_REQUEST_CACHE_FS_HPP
#define WRITE_REQUEST_CACHE_FS_HPP
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

#endif // WRITE_REQUEST_CACHE_FS_HPP
