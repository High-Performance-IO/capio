#ifndef CAPIO_POSIX_HANDLERS_WRITE_HPP
#define CAPIO_POSIX_HANDLERS_WRITE_HPP

#include "utils/functions.hpp"
#include "utils/requests.hpp"

inline ssize_t capio_write(int fd, const void *buffer, off64_t count, long tid) {
    START_LOG(tid, "call(fd=%d, buf=0x%08x, count=%ld)", fd, buffer, count);

    if (exists_capio_fd(fd)) {
        if (count > SSIZE_MAX) {
            ERR_EXIT("Capio does not support writes bigger than "
                     "SSIZE_MAX yet");
        }
        off64_t count_off = count;
        write_request(fd, count_off, tid);
        write_data(tid, buffer, count);

        return count;
    } else {
        return POSIX_SYSCALL_REQUEST_SKIP;
    }
}

inline ssize_t capio_writev(int fd, const struct iovec *iov, int iovcnt, long tid) {
    START_LOG(tid, "call(fd=%d, iov.iov_base=0x%08x, iov.iov_len=%ld, iovcnt=%d)", fd,
              iov->iov_base, iov->iov_len, iovcnt);

    if (exists_capio_fd(fd)) {
        LOG("fd %d exists and is a capio fd", fd);
        ssize_t tot_bytes = 0;
        ssize_t res       = 0;
        int i             = 0;
        LOG("iov setup completed. starting write request loop");
        while (i < iovcnt && res >= 0) {
            size_t iov_len = iov[i].iov_len;
            if (iov_len != 0) {
                res = capio_write(fd, iov[i].iov_base, iov[i].iov_len, tid);
                tot_bytes += res;
            }
            ++i;
        }
        if (res == -1) {
            return POSIX_SYSCALL_ERRNO;
        } else {
            return tot_bytes;
        }
    } else {
        LOG("fd %d is not a capio fd. returning -2", fd);
        return POSIX_SYSCALL_REQUEST_SKIP;
    }
}

int write_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd         = static_cast<int>(arg0);
    const auto *buf = reinterpret_cast<const void *>(arg1);
    auto count      = static_cast<off64_t>(arg2);
    long tid        = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_write(fd, buf, count, tid), result);
}

int writev_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd         = static_cast<int>(arg0);
    const auto *iov = reinterpret_cast<const struct iovec *>(arg1);
    auto iovcnt     = static_cast<int>(arg2);
    long tid        = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_writev(fd, iov, iovcnt, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_WRITE_HPP
