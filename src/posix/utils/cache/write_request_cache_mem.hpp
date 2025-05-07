#ifndef WRITE_REQUEST_CACHE_MEM_HPP
#define WRITE_REQUEST_CACHE_MEM_HPP

class WriteRequestCacheMEM {
    char *_cache;
    long _tid;
    int _fd;
    off64_t _max_line_size, _actual_size;
    capio_off64_t _last_write_end, _last_write_begin;

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

protected:
    void write_request(const off64_t count, const long tid, const char *path,
                       const capio_off64_t offset) const {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, count=%ld, offset=%llu)", path, count,
                  offset);
        char req[CAPIO_REQ_MAX_SIZE];

        sprintf(req, "%04d %ld %s %llu %ld", CAPIO_REQUEST_WRITE_MEM, tid, path, offset, count);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    }

public:
    explicit WriteRequestCacheMEM(off64_t line_size = get_cache_line_size())
        : _cache(nullptr), _tid(capio_syscall(SYS_gettid)), _fd(-1), _max_line_size(line_size),
          _actual_size(0), _last_write_end(-1), _last_write_begin(0) {
    }

    ~WriteRequestCacheMEM() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        this->flush();
    }

    void flush() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        if (_actual_size != 0) {
            LOG("Actual size: %ld", _actual_size);
            write_request(_actual_size, _tid, get_capio_fd_path(_fd).c_str(), _last_write_begin);
            _cache = nullptr;
            _actual_size = 0;
        }
        LOG("Flush completed");
    }

    void write(int fd, const void *buffer, off64_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, buffer=0x%08x, count=%ld)", fd, buffer,
                  count);

        if (_fd != fd) {
            LOG("changed fd from %d to %d: flushing", _fd, fd);
            flush();
            _fd = fd;
            _last_write_end = -1;
        }

        // Check if a seek has occurred before and in case in which case flush the cache
        // and update the offset to the new value
        if (_last_write_end != get_capio_fd_offset(_fd)) {
            flush();
            _last_write_begin = get_capio_fd_offset(_fd) + _actual_size;
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
                write_request(count, _tid, get_capio_fd_path(_fd).c_str(), _last_write_begin);
                cts_queue->write(static_cast<const char *>(buffer), count);
            } else {
                LOG("count - _actual_size %ld <= _max_line_size %ld", count - _actual_size,
                    _max_line_size);
                _write(count, buffer);
            }
        }
        _last_write_end = get_capio_fd_offset(fd) + count;
        set_capio_fd_offset(fd, _last_write_end);
    }
};

#endif // WRITE_REQUEST_CACHE_MEM_HPP
