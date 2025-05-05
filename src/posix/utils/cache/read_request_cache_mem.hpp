#ifndef READ_REQUEST_CACHE_MEM_HPP
#define READ_REQUEST_CACHE_MEM_HPP

class ReadRequestCacheMEM {
    char *_cache;
    long _tid;
    int _fd;
    capio_off64_t _max_line_size, _actual_size, _cache_offset;
    capio_off64_t _last_read_end, _real_file_size_commmitted;
    bool committed = false;

    /**
     * Copy data from the cache internal buffer to the target buffer
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
                                             const long tid, bool use_cache = true) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, count=%llu, tid=%ld, load_data=%s)", fd,
                  count, tid, use_cache ? "true" : "false");
        char req[CAPIO_REQ_MAX_SIZE];

        // send as the last parameter to the server the maximum amount of data that can be read into
        // a single line of cache

        auto read_begin_offset = get_capio_fd_offset(fd);

        sprintf(req, "%04d %ld %llu %llu %llu %d %s", CAPIO_REQUEST_READ_MEM, tid,
                read_begin_offset, count, _max_line_size, use_cache, get_capio_fd_path(fd).c_str());
        LOG("Sending read request %s", req);
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        capio_off64_t stc_queue_read = bufs_response->at(tid)->read();
        LOG("Response to request is %llu", stc_queue_read);

        if (stc_queue_read >= 0x8000000000000000) {
            committed = true;
            stc_queue_read -= 0x8000000000000000;
            _real_file_size_commmitted = stc_queue_read;
            LOG("File is commited. Actual offset is: %ld", stc_queue_read);
        }

        if (use_cache) {
            stc_queue->read(_cache, stc_queue_read);
            _cache_offset = 0;
            LOG("Completed fetch of data from server");
        } else {
            _actual_size = 0;
            _cache_offset = 0;
            LOG("Data has not been loaded from server, as load_data==false."
                " Load will occur indipendently");
        }

        return stc_queue_read;
    }

public:
    explicit ReadRequestCacheMEM(const long line_size = get_posix_read_cache_line_size())
        : _cache(nullptr), _tid(capio_syscall(SYS_gettid)), _fd(-1), _max_line_size(line_size),
          _actual_size(0), _cache_offset(0), _last_read_end(-1) {
        _cache = new char[_max_line_size];
    }

    ~ReadRequestCacheMEM() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        delete[] _cache;
    }

    void flush() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        if (_cache_offset != _actual_size) {
            _actual_size = _cache_offset = 0;
        }
        committed = false;
        _real_file_size_commmitted = -1;
    }

    long read(const int fd, void *buffer, off64_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, count=%ld)", fd, count);

        long actual_read_size = 0;

        if (_fd != fd) {
            LOG("changed fd from %d to %d: flushing", _fd, fd);
            flush();
            _fd = fd;
            _last_read_end = get_capio_fd_offset(fd);
        }

        // Check if a seek has occurred before and in case in which case flush the cache
        // and update the offset to the new value
        if (_last_read_end != get_capio_fd_offset(_fd)) {
            LOG("A seek() has occurred (_last_read_end=%llu, get_capio_fd_offset=%llu). Performing "
                "flush().",
                _last_read_end, get_capio_fd_offset(_fd));
            flush();
            _last_read_end = get_capio_fd_offset(_fd);
        }

        if (committed && _real_file_size_commmitted == _last_read_end) {
            LOG("All file content has been read. Returning 0");
            return 0;
        }

        /*
         * Check: if read size is greater than the capability of the cache line, bypass
         * the cache and perform a read directly to the provided buffer
         */
        if (count > _max_line_size) {
            LOG("count > _max_line_size. Bypassing cache. Performing read() directly to buffer.");
            const auto _read_size = read_request(_fd, count, _tid, false);
            stc_queue->read(static_cast<char *>(buffer), _read_size);
            return _read_size;
        }

        // Check if cache is empty or if all the content of the cache has been already consumed
        if (_actual_size == 0 || _actual_size == _cache_offset) {
            LOG("No data is present locally. performing request.");
            const auto size = count < _max_line_size ? count : _max_line_size;
            _actual_size = read_request(_fd, size, _tid);

            // Update count for current request. If count exceeds _actual_size, resize it to not
            // exceeds the available size on posix application
            count = std::min(static_cast<capio_off64_t>(count), _actual_size);
        }

        if (count <= _max_line_size - _cache_offset) {
            // There is enough data to perform a read
            LOG("The requested amount of data can be served without performing a request");
            _read(buffer, count);
            actual_read_size = count;
            _last_read_end = get_capio_fd_offset(_fd) + count;
            set_capio_fd_offset(fd, _last_read_end);

        } else {
            // There could be some data available already on the cache. Copy that first and then
            // proceed to request the other missing data

            const auto first_copy_size =
                std::min(_actual_size - _cache_offset, static_cast<capio_off64_t>(count));

            LOG("Data (or part of it) might be already present. performing first copy of"
                " std::min(_actual_size(%llu) - _cache_offset(%llu), count(%llu) = %ld",
                _actual_size, _cache_offset, count, first_copy_size);

            _read(buffer, first_copy_size);
            _last_read_end = get_capio_fd_offset(_fd) + first_copy_size;
            set_capio_fd_offset(fd, get_capio_fd_offset(fd) + first_copy_size);
            actual_read_size = first_copy_size;
            LOG("actual_read_size incremented to: %ld", actual_read_size);

            // Compute the remaining amount of data to send to client
            auto remaining_size = count - first_copy_size;
            capio_off64_t copy_offset = first_copy_size;

            while (copy_offset < count && !committed) {
                LOG("Need to request still %ld of data from server component", count - copy_offset);
                // request a line from the server component through a request
                _actual_size = read_request(_fd, remaining_size, _tid);

                if (committed) {
                    LOG("File has resulted in a commit message. Exiting loop");
                    break;
                }

                LOG("Available size after request: %ld", _actual_size);
                // compute the amount of data that is going to be sent to the client application
                auto size_to_send_to_client =
                    remaining_size < _actual_size ? remaining_size : _actual_size;

                LOG("Sending %ld of data to posix application", size_to_send_to_client);
                _read(static_cast<char *>(buffer) + copy_offset, size_to_send_to_client);
                actual_read_size += size_to_send_to_client;
                LOG("actual_read_size incremented to: %ld", actual_read_size);

                copy_offset += size_to_send_to_client;
                remaining_size -= size_to_send_to_client;

                _last_read_end = get_capio_fd_offset(_fd) + size_to_send_to_client;
                set_capio_fd_offset(fd, _last_read_end);
            }
        }

        LOG("Read return value: %ld (_last_Read_end = %llu)", actual_read_size, _last_read_end);
        return actual_read_size;
    }
};
#endif // READ_REQUEST_CACHE_MEM_HPP