#ifndef CAPIO_POSIX_HANDLERS_WRITE_HPP
#define CAPIO_POSIX_HANDLERS_WRITE_HPP

#if defined(SYS_write) || defined(SYS_writev)

#include "utils/common.hpp"
#include "utils/requests.hpp"

inline off64_t capio_write(int fd, capio_off64_t count, pid_t tid) {
    START_LOG(tid, "call(fd=%d, count=%ld)", fd, count);

    if (exists_capio_fd(fd)) {
        LOG("File needs to be handled");
        write_request_cache->write_request(get_capio_fd_path(fd), count, tid, fd);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

int write_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd    = static_cast<int>(arg0);
    auto count = static_cast<capio_off64_t>(arg2);
    auto tid   = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    return posix_return_value(capio_write(fd, count, tid), result);
}

int writev_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd     = static_cast<int>(arg0);
    auto iovcnt = static_cast<int>(arg2);
    long tid    = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_write(fd, iovcnt, tid), result);
}

#endif // SYS_write || SYS_writev
#endif // CAPIO_POSIX_HANDLERS_WRITE_HPP
