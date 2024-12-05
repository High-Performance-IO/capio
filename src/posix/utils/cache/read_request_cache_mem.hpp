#ifndef READ_REQUEST_CACHE_MEM_HPP
#define READ_REQUEST_CACHE_MEM_HPP
class ReadRequestCacheMEM {
  private:
    char *_cache;
    long _tid;
    int _fd;
    capio_off64_t _max_line_size, _actual_size, _cache_offset;
    capio_off64_t _last_read_end;

    /**
     * Copy data from the cache internal buffer to target buffer
     * @param buffer
     * @param count
     */
    void _read(void *buffer, capio_off64_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);

        if (count > 0) {
            memcpy(buffer, _cache + _cache_offset, count);
            LOG("Read %ld. adding it to _cache_offset of value %ld", count, _cache_offset);
            _cache_offset += count;
        }
    }

  protected:
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
    explicit ReadRequestCacheMEM(const long line_size             = get_cache_line_size(),
                                 const std::string &workflow_name = get_capio_workflow_name())
        : _cache(nullptr), _tid(capio_syscall(SYS_gettid)), _fd(-1), _max_line_size(line_size),
          _actual_size(0), _cache_offset(0), _last_read_end(-1) {
        _cache = new char[_max_line_size];
    }

    ~ReadRequestCacheMEM() { delete[] _cache; }

    inline void flush() {
        START_LOG(capio_syscall(SYS_gettid), "call()");

        if (_cache_offset != _actual_size) {
            _actual_size = _cache_offset = 0;
        }
    }

    inline capio_off64_t read(const int fd, void *buffer, off64_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, count=%ld, is_getdents=%s)", fd, count);

        if (_fd != fd) {
            LOG("changed fd from %d to %d: flushing", _fd, fd);
            flush();
            _fd = fd;
        }

        // Check if a seek has occurred before and in case in which case flush the cache
        // and update the offset to the new value
        if (_last_read_end != get_capio_fd_offset(_fd)) {
            flush();
            _last_read_end = get_capio_fd_offset(_fd);
        }

        if (count <= _max_line_size - _cache_offset) {
            // There is enough data to perform a read
            LOG("The requested amount of data can be served without performing a request");
            _read(buffer, count);
            _last_read_end = get_capio_fd_offset(_fd) + count;

        } else {
            // There is not enough data and I need to get more data
            auto first_copy_size = _max_line_size - _cache_offset;
            _read(buffer, first_copy_size);
            auto remaining_size = count - first_copy_size;
            while (remaining_size > 0) {
                // richiedo una linea
                read_request(_fd, count, _tid);
                // mi salvo la linea
                stc_queue->read(_cache, _max_line_size);
                // mando la linea al client
                _read(buffer, remaining_size > _max_line_size ? _max_line_size : remaining_size);

                remaining_size -= _max_line_size;
            }
        }

        // questo probabilmente e' sbagliato
        _last_read_end = get_capio_fd_offset(_fd) + count;

        set_capio_fd_offset(fd, _last_read_end);
        return 0;
    }
};
#endif // READ_REQUEST_CACHE_MEM_HPP
