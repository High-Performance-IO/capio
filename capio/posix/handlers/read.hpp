#ifndef CAPIO_POSIX_HANDLERS_READ_HPP
#define CAPIO_POSIX_HANDLERS_READ_HPP

#if defined(SYS_read) || defined(SYS_readv)

#include "common/constants.hpp"

#include "utils/common.hpp"
#include "utils/data.hpp"
#include "utils/tid.hpp"

inline off64_t capio_read(int fd, void *buffer, off64_t count, long tid) {
    START_LOG(tid, "call(fd=%d, buf=0x%08x, count=%ld)", fd, buffer, count);

    if (fd_table->contains(fd)) {
        return capio_cached_read(fd, buffer, count);
    } else {
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }
}

inline ssize_t capio_readv(int fd, const struct iovec *iov, int iovcnt, long tid) {
    START_LOG(tid, "call(fd=%d, iov.iov_base=0x%08x, iov.iov_len=%ld, iovcnt=%d)", fd,
              iov->iov_base, iov->iov_len, iovcnt);

    if (fd_table->contains(fd)) {
        LOG("fd %d exists and is a capio fd", fd);
        ssize_t tot_bytes = 0;
        ssize_t res       = 0;
        int i             = 0;
        LOG("iov setup completed. starting read request loop");
        while (i < iovcnt && res >= 0) {
            size_t iov_len = iov[i].iov_len;
            if (iov_len != 0) {
                res = capio_read(fd, iov[i].iov_base, iov[i].iov_len, tid);
                if (res == -1) {
                    return CAPIO_POSIX_SYSCALL_ERRNO;
                }
                tot_bytes += res;
            }
            ++i;
        }
        return tot_bytes;
    } else {
        LOG("fd %d is not a capio fd. returning -2", fd);
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }
}

/**
 * FS variant: block until the bytes from @p offset (the file offset if negative) to @p offset +
 * @p count can be read. Then the kernel runs the call that hands them out.
 *
 * The data are on the file system, so the posix component reads them itself, through the kernel:
 * only the wait goes through the server. That is faster, and keeps the server out of the path of
 * the data. Every such call waits for what it needs:
 *
 *   read, readv, pread, preadv, preadv2   offset + count
 *   copy_file_range, sendfile, splice     offset + 1: their callers loop on short results
 *   SEEK_DATA, SEEK_HOLE                  offset + 1
 *   SEEK_END, FICLONE                     the commit (CAPIO_UNTIL_COMMIT_FS): the end of a stream
 * is not known before, and clones cannot stream
 *
 * mmap does not wait: CAPIO and libc map memory with it themselves, so it is not intercepted. A
 * mapping of a file not committed yet may miss its end.
 *
 * In update mode it is the commit anyway. stat and fstat never wait for data: in no_update they
 * return the current size.
 */
inline void capio_wait_for_data_fs(int fd, off64_t offset, off64_t count, long tid) {
    START_LOG(tid, "call(fd=%d, offset=%ld, count=%ld)", fd, offset, count);

    struct stat info{};
    if (!fd_table->contains(fd) || (fd_table->flags(fd) & O_ACCMODE) == O_WRONLY ||
        syscall_no_intercept(SYS_fstat, fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        // not CAPIO, a writer (fopen(..., "a") seeks to the end), or nothing that streams
        return;
    }
    if (offset < 0) {
        offset = std::max<long>(syscall_no_intercept(SYS_lseek, fd, 0, SEEK_CUR), 0);
    }
    read_request_cache->read(fd, count > LLONG_MAX - offset ? LLONG_MAX : offset + count, tid);
}

inline off64_t capio_read_fs(int fd, void *buffer, off64_t count, long tid) {
    START_LOG(tid, "call(fd=%d, buf=0x%08x, count=%ld)", fd, buffer, count);
    capio_wait_for_data_fs(fd, -1, count, tid);
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

/// @brief The bytes of @p iovcnt buffers, LLONG_MAX if more
inline off64_t capio_iov_length(const struct iovec *iov, int iovcnt) {
    off64_t count = 0;
    for (int i = 0; i < iovcnt; ++i) {
        const auto length = static_cast<off64_t>(iov[i].iov_len);
        count             = length > LLONG_MAX - count ? LLONG_MAX : count + length;
    }
    return count;
}

inline ssize_t capio_readv_fs(int fd, const struct iovec *iov, int iovcnt, long tid) {
    START_LOG(tid, "call(fd=%d, iovcnt=%d)", fd, iovcnt);
    return capio_read_fs(fd, nullptr, capio_iov_length(iov, iovcnt), tid);
}

int read_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd       = static_cast<int>(arg0);
    void *buffer = reinterpret_cast<void *>(arg1);
    auto count   = static_cast<off64_t>(arg2);

    MEMORY_MODE({ return posix_return_value(capio_read(fd, buffer, count, capio_tid()), result); })
    FS_MODE({ return posix_return_value(capio_read_fs(fd, buffer, count, capio_tid()), result); })
}

int readv_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd         = static_cast<int>(arg0);
    const auto *iov = reinterpret_cast<const struct iovec *>(arg1);
    auto iovcnt     = static_cast<int>(arg2);

    MEMORY_MODE({ return posix_return_value(capio_readv(fd, iov, iovcnt, capio_tid()), result); })
    FS_MODE({ return posix_return_value(capio_readv_fs(fd, iov, iovcnt, capio_tid()), result); })
}

#ifdef SYS_pread64
int pread_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    // pread(fd, buffer, count, offset)
    const auto fd                      = static_cast<int>(arg0);
    [[maybe_unused]] const auto count  = static_cast<off64_t>(arg2); // FS variant, and the log
    [[maybe_unused]] const auto offset = static_cast<off64_t>(arg3);
    START_LOG(capio_tid(), "call(fd=%d, count=%ld, offset=%ld)", fd, count, offset);

    MEMORY_MODE({
        if (fd_table->contains(fd)) {
            throw CapioUnimplementedException("pread");
        }
        return CAPIO_POSIX_SYSCALL_SKIP;
    })
    FS_MODE({
        // A negative offset is EINVAL, from the kernel
        if (offset >= 0) {
            capio_wait_for_data_fs(fd, offset, count, capio_tid());
        }
        return CAPIO_POSIX_SYSCALL_SKIP;
    })
}
#endif

#if defined(SYS_preadv) || defined(SYS_preadv2)
int preadv_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    // preadv(fd, iov, iovcnt, offset), and preadv2, which takes -1 for the file offset
    const auto fd                      = static_cast<int>(arg0);
    [[maybe_unused]] const auto *iov   = reinterpret_cast<const struct iovec *>(arg1); // FS variant
    [[maybe_unused]] const auto iovcnt = static_cast<int>(arg2);
    [[maybe_unused]] const auto offset = static_cast<off64_t>(arg3);
    START_LOG(capio_tid(), "call(fd=%d, iovcnt=%d, offset=%ld)", fd, iovcnt, offset);

    MEMORY_MODE({
        if (fd_table->contains(fd)) {
            throw CapioUnimplementedException("preadv");
        }
        return CAPIO_POSIX_SYSCALL_SKIP;
    })
    FS_MODE({
        if (offset >= -1) {
            capio_wait_for_data_fs(fd, offset, capio_iov_length(iov, iovcnt), capio_tid());
        }
        return CAPIO_POSIX_SYSCALL_SKIP;
    })
}
#endif

#endif // SYS_read || SYS_readv
#endif // CAPIO_POSIX_HANDLERS_READ_HPP
