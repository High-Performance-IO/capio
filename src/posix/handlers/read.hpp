#ifndef CAPIO_POSIX_HANDLERS_READ_HPP
#define CAPIO_POSIX_HANDLERS_READ_HPP

#if defined(SYS_read) || defined(SYS_readv)

#include "utils/data.hpp"

inline ssize_t capio_read(int fd, void *buffer, off64_t count, long tid) {
    START_LOG(tid, "call(fd=%d, buf=0x%08x, count=%ld)", fd, buffer, count);

    if (exists_capio_fd(fd)) {
        if (count >= SSIZE_MAX) {
            ERR_EXIT("CAPIO does not support read bigger than SSIZE_MAX yet");
        }
        off64_t offset      = get_capio_fd_offset(fd);
        off64_t end_of_read = read_request(fd, count, tid);
        off64_t bytes_read  = end_of_read - offset;
        read_data(tid, fd, buffer, bytes_read);
        return bytes_read;
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

#endif // SYS_read || SYS_readv
#endif // CAPIO_POSIX_HANDLERS_READ_HPP
