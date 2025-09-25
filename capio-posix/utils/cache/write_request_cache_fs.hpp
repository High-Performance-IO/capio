#ifndef WRITE_REQUEST_CACHE_FS_HPP
#define WRITE_REQUEST_CACHE_FS_HPP

class WriteRequestCacheFS {
    std::unordered_map<std::string, capio_off64_t> capio_internal_write_offsets;

    const capio_off64_t _max_size;

    std::filesystem::path current_path;

    // non-blocking as write is not in the pre port of CAPIO semantics
    inline void _write_request(capio_off64_t count, const long tid, const long fd) const {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, count=%ld, tid=%ld)",
                  current_path.c_str(), count, tid);
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %ld %ld %s %llu", CAPIO_REQUEST_WRITE, tid, fd, current_path.c_str(),
                count);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    }

  public:
    explicit WriteRequestCacheFS() : _max_size(get_capio_write_cache_size()) {}

    ~WriteRequestCacheFS() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        const int tid = capio_syscall(SYS_gettid);
        for (auto &[path, size] : capio_internal_write_offsets) {
            this->flush(path, tid, -1);
        }
    }

    void write_request(const std::filesystem::path &path, int tid, int fd, long count) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld, fd=%ld, count=%ld)",
                  path.c_str(), tid, fd, count);

        if (!capio_internal_write_offsets.contains(path.c_str())) {
            capio_internal_write_offsets[path.c_str()] = 0;
        }

        capio_internal_write_offsets[path.c_str()] += count;

        if (capio_internal_write_offsets[path.c_str()] >= _max_size) {
            LOG("exceeded maximum cache size. flushing...");
            this->flush(path, tid, fd);
            capio_internal_write_offsets[path.c_str()] = 0;
        }
    };

    void flush(const std::filesystem::path &path, long tid, int fd) {
        START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, path=%s)", tid, path.c_str());

        LOG("Performing write to SHM");
        _write_request(capio_internal_write_offsets[path.c_str()], tid, fd);
    }
};

#endif // WRITE_REQUEST_CACHE_FS_HPP