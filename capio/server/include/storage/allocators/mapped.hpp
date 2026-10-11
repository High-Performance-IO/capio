
#ifndef CAPIO_MAPPED_HPP
#define CAPIO_MAPPED_HPP

/// @brief mmap allocation strategy: maps a persistent file as shared memory.
struct MappedAllocator {
    int _fd          = -1; ///< backing file descriptor
    off64_t _map_len = 0;  ///< original mmap length, kept so commit-shrink still unmaps fully

    /**
     * @brief Map the backing file at @p path (creating it), capped by available memory.
     * @param data    Buffer base (updated).
     * @param size    Buffer logical size (updated).
     * @param path    Backing file path.
     * @param requested Desired buffer size.
     * @return True on success; on failure rolls back to empty and returns false.
     */
    bool allocate(char *&data, off64_t &size, const std::filesystem::path &path,
                  const off64_t requested) {
        size = computeAllocableMemQuota(requested);
        _fd  = ::open(path.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
        if (_fd < 0 || ftruncate(_fd, size) == -1) {
            if (_fd >= 0) {
                ::close(_fd);
                _fd = -1;
            }
            return false;
        }
        data = static_cast<char *>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0));
        if (data == MAP_FAILED) {
            ::close(_fd);
            _fd  = -1;
            data = nullptr;
            return false;
        }
        _map_len = size;
        return true;
    }

    /**
     * @brief Shrink the mapped buffer to @p committed and close its backing fd.
     * @param size      Buffer logical size (updated).
     * @param committed Size to retain on disk.
     * @return True on success; false on ftruncate/close failure.
     */
    bool commit(off64_t &size, const off64_t committed) {
        if (ftruncate(_fd, committed) == -1) {
            return false;
        }
        size = committed;
        if (::close(_fd) == -1) {
            return false;
        }
        _fd = -1;
        return true;
    }

    /** @brief Unmap the buffer and close the backing fd. */
    void freeBuffer(char *data) {
        if (data != nullptr) {
            ::munmap(data, _map_len);
        }
        if (_fd >= 0) {
            ::close(_fd);
            _fd = -1;
        }
        _map_len = 0;
    }
};
#endif // CAPIO_MAPPED_HPP
