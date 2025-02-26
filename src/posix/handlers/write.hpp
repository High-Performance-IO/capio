#ifndef CAPIO_POSIX_HANDLERS_WRITE_HPP
#define CAPIO_POSIX_HANDLERS_WRITE_HPP

#include "utils/common.hpp"
#include "utils/requests.hpp"

inline off64_t capio_write_fs(int fd, capio_off64_t count, pid_t tid) {
    START_LOG(tid, "call(fd=%d, count=%ld)", fd, count);

    write_request_cache_fs->write_request(get_capio_fd_path(fd), count, tid, fd);

    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

inline off64_t capio_write_mem(int fd, char *buffer, capio_off64_t count, pid_t tid) {

    START_LOG(tid, "call(fd=%d, count=%ld)", fd, count);
    return 0;
}

#if defined(SYS_write)
int write_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd     = static_cast<int>(arg0);
    auto buffer = reinterpret_cast<char *>(arg1);
    auto count  = static_cast<capio_off64_t>(arg2);
    auto tid    = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    START_LOG(tid, "call(fd=%d, buffer=%p, count=%ld, id=%ld)", fd, buffer, count, tid);
    if (!exists_capio_fd(fd)) {
        LOG("FD %d is not handled by capio... skipping syscall", fd);
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    auto write_result = store_file_in_memory(get_capio_fd_path(fd), tid)
                            ? capio_write_mem(fd, buffer, count, tid)
                            : capio_write_fs(fd, count, tid);

    return posix_return_value(write_result, result);
}
#endif // SYS_write

#if defined(SYS_writev)
int writev_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd     = static_cast<int>(arg0);
    auto buffer = reinterpret_cast<char *>(arg1);
    auto iovcnt = static_cast<int>(arg2);
    long tid    = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, buffer=%p, count=%ld, id=%ld)", fd, buffer, iovcnt, tid);
    if (!exists_capio_fd(fd)) {
        LOG("FD %d is not handled by capio... skipping syscall", fd);
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    auto write_result = store_file_in_memory(get_capio_fd_path(fd), tid)
                            ? capio_write_mem(fd, buffer, iovcnt, tid)
                            : capio_write_fs(fd, iovcnt, tid);

    return posix_return_value(write_result, result);
}
#endif // SYS_writev

#endif // CAPIO_POSIX_HANDLERS_WRITE_HPP
