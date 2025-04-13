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
    [[nodiscard]] capio_off64_t read_request(const int fd, const capio_off64_t count,
                                             const long tid) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, count=%llu, tid=%ld)", fd, count, tid);
        char req[CAPIO_REQ_MAX_SIZE];

        // send as last parameter to the server the maximum amount of data that can be read into a
        // single line of cache

        auto read_begin_offset = get_capio_fd_offset(fd);

        sprintf(req, "%04d %ld %llu %llu %llu %s", CAPIO_REQUEST_READ_MEM, tid, read_begin_offset,
                count, _max_line_size, get_capio_fd_path(fd).c_str());
        LOG("Sending read request %s", req);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        capio_off64_t stc_queue_read;
        bufs_response->at(tid)->read(&stc_queue_read);
        LOG("Response to request is %ld", stc_queue_read);

        // FIXME: if count > _max_line_size, a deadlock or SEGFAULT is foreseen Fix it asap.
        // FIXME: still this might not occur as the read() method should protect from this event
        auto read_size = count;
        while (read_size > 0) {
            const capio_off64_t tmp_read_size =
                read_size > _max_line_size ? _max_line_size : read_size;
            stc_queue->read(_cache, tmp_read_size);
            _cache_offset = 0;
            read_size -= tmp_read_size;
        }

        LOG("Completed fetch of data from server");

        return stc_queue_read;
    }

  public:
    explicit ReadRequestCacheMEM(const long line_size = capio_config->CAPIO_POSIX_CACHE_LINE_SIZE)
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

    void read(const int fd, void *buffer, off64_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, count=%ld)", fd, count);
        if (_fd != fd) {
            LOG("changed fd from %d to %d: flushing", _fd, fd);
            flush();
            _fd            = fd;
            _last_read_end = get_capio_fd_offset(_fd);
        }

        // Check if a seek has occurred before and in case in which case flush the cache
        // and update the offset to the new value
        if (_last_read_end != get_capio_fd_offset(_fd)) {
            flush();
            _last_read_end = get_capio_fd_offset(_fd);
        }

        if (_actual_size == 0) {
            const auto size = count < _max_line_size ? count : _max_line_size;
            read_request(_fd, size, _tid);
        }

        if (count <= _max_line_size - _cache_offset) {
            // There is enough data to perform a read
            LOG("The requested amount of data can be served without performing a request");
            _read(buffer, count);

        } else {
            // There could be some data available already on the cache. Copy that first and then
            // proceed to request the other missing data

            const auto first_copy_size = _max_line_size - _cache_offset;

            _read(buffer, first_copy_size);
            set_capio_fd_offset(fd, get_capio_fd_offset(fd) + first_copy_size);

            // Compute the remaining amount of data to send to client
            auto remaining_size       = count - first_copy_size;
            capio_off64_t copy_offset = first_copy_size;

            while (copy_offset < count) {

                // request a line from the server component through a request
                auto available_size = read_request(_fd, remaining_size, _tid);
                // compute the amount of data that is going to be sent to the client application
                auto size_to_send_to_client =
                    remaining_size < available_size ? remaining_size : available_size;

                _read(static_cast<char *>(buffer) + copy_offset, size_to_send_to_client);

                copy_offset += size_to_send_to_client;
                remaining_size -= size_to_send_to_client;
            }
        }

        _last_read_end = get_capio_fd_offset(_fd) + count;
        set_capio_fd_offset(fd, _last_read_end);
    }
};
#endif // READ_REQUEST_CACHE_MEM_HPP
