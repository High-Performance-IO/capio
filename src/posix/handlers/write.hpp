#ifndef CAPIO_POSIX_HANDLERS_WRITE_HPP
#define CAPIO_POSIX_HANDLERS_WRITE_HPP

#if defined(SYS_write) || defined(SYS_writev)

#include "utils/common.hpp"
#include "utils/requests.hpp"

inline off64_t capio_write(int fd, const void *buffer, off64_t count, long tid) {
    START_LOG(tid, "call(fd=%d, buf=0x%08x, count=%ld)", fd, buffer, count);

    if (exists_capio_fd(fd)) {
        write_request(get_capio_fd_path(fd), count, tid);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
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

    return posix_return_value(capio_write(fd, iov, iovcnt, tid), result);
}

#endif // SYS_write || SYS_writev
#endif // CAPIO_POSIX_HANDLERS_WRITE_HPP
