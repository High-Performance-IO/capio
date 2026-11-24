#ifndef CAPIO_POSIX_HANDLERS_READ_HPP
#define CAPIO_POSIX_HANDLERS_READ_HPP

#if defined(SYS_read) || defined(SYS_readv)

#include "utils/data.hpp"

inline off64_t capio_read(int fd, void *buffer, off64_t count, long tid) {
    START_LOG(tid, "call(fd=%d, buf=0x%08x, count=%ld)", fd, buffer, count);

    if (exists_capio_fd(fd)) {
        if (count >= SSIZE_MAX) {
            ERR_EXIT("CAPIO does not support read bigger than SSIZE_MAX yet");
        }

        return capio_cached_read(fd, buffer, count);
    } else {
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }
}

inline ssize_t capio_readv(int fd, const struct iovec *iov, int iovcnt, long tid) {
    START_LOG(tid, "call(fd=%d, iov.iov_base=0x%08x, iov.iov_len=%ld, iovcnt=%d)", fd,
              iov->iov_base, iov->iov_len, iovcnt);

    if (exists_capio_fd(fd)) {
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

int read_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd       = static_cast<int>(arg0);
    void *buffer = reinterpret_cast<void *>(arg1);
    auto count   = static_cast<off64_t>(arg2);
    long tid     = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_read(fd, buffer, count, tid), result);
}

int readv_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd         = static_cast<int>(arg0);
    const auto *iov = reinterpret_cast<const struct iovec *>(arg1);
    auto iovcnt     = static_cast<int>(arg2);
    long tid        = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_readv(fd, iov, iovcnt, tid), result);
}

#if !CAPIO_MEM_STREAM
/**
 * FS variant: block until the bytes up to the file offset + @p count can be read, then the kernel
 * runs the read. LLONG_MAX, for a count that goes past it, waits for the commit.
 */
inline void capio_read_fs(int fd, off64_t count, long tid) {
    START_LOG(tid, "call(fd=%d, count=%ld)", fd, count);

    struct stat info{};
    if ((get_capio_fd_flags(fd) & O_ACCMODE) == O_WRONLY ||
        syscall_no_intercept(SYS_fstat, fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        return; // a writer (fopen(..., "a") seeks to the end), or nothing that streams
    }
    const off64_t offset = std::max<long>(syscall_no_intercept(SYS_lseek, fd, 0, SEEK_CUR), 0);
    read_request_cache->read(fd, count > LLONG_MAX - offset ? LLONG_MAX : offset + count, tid);
}

int read_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    const int fd = static_cast<int>(arg0);
    if (exists_capio_fd(fd)) {
        capio_read_fs(fd, static_cast<off64_t>(arg2), syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int readv_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const int fd = static_cast<int>(arg0);
    if (exists_capio_fd(fd)) {
        const auto *iov = reinterpret_cast<const struct iovec *>(arg1);
        off64_t count   = 0;
        for (int i = 0; i < static_cast<int>(arg2); ++i) {
            const auto length = static_cast<off64_t>(iov[i].iov_len);
            count             = length > LLONG_MAX - count ? LLONG_MAX : count + length;
        }
        capio_read_fs(fd, count, syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif // !CAPIO_MEM_STREAM

#endif // SYS_read || SYS_readv
#endif // CAPIO_POSIX_HANDLERS_READ_HPP
