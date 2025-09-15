#ifndef CAPIO_POSIX_HANDLERS_WRITE_HPP
#define CAPIO_POSIX_HANDLERS_WRITE_HPP

#include "utils/common.hpp"
#include "utils/requests.hpp"

inline off64_t capio_write_fs(int fd, capio_off64_t count, pid_t tid) {
    START_LOG(tid, "call(fd=%d, count=%ld)", fd, count);

    write_request_cache_fs->write_request(get_capio_fd_path(fd), count, tid, fd);

    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

inline off64_t capio_writev_fs(int fd, char *buffer, off64_t count, pid_t tid) {
    START_LOG(tid, "Handling FS write within writev");
    capio_write_fs(fd, count, tid);
    const off64_t write_count = syscall_no_intercept(SYS_write, fd, buffer, count);
    LOG("Wrote %ld bytes", write_count);
    return write_count;
}

inline off64_t capio_write_mem(int fd, char *buffer, capio_off64_t count, pid_t tid) {
    START_LOG(tid, "call(fd=%d, count=%ld)", fd, count);
    write_request_cache_mem->write(fd, buffer, count);
    return count;
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

    LOG("Write result: %ld", write_result);

    return posix_return_value(write_result, result);
}
#endif // SYS_write

#if defined(SYS_writev)
int writev_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd     = static_cast<int>(arg0);
    auto io_vec = reinterpret_cast<const struct iovec *>(arg1);
    auto iovcnt = static_cast<int>(arg2);
    long tid    = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, buffer=%p, count=%ld, pid=%ld)", fd, io_vec->iov_base,
              io_vec->iov_len, tid);
    if (!exists_capio_fd(fd)) {
        LOG("FD %d is not handled by CAPIO... skipping syscall", fd);
        return posix_return_value(CAPIO_POSIX_SYSCALL_REQUEST_SKIP, result);
    }

    LOG("Need to handle %ld IOVEC objects", iovcnt);
    int write_result = 0;
    for (auto i = 0; i < iovcnt; ++i) {
        const auto [iov_base, iov_len] = io_vec[i];
        if (iov_len == 0) {
            LOG("Size of IOVEC is 0. Skipping write request");
            continue;
        }
        LOG("Handling IOVEC elements %d of size %ld", i, iov_len);
        write_result += store_file_in_memory(get_capio_fd_path(fd), tid)
                            ? capio_write_mem(fd, static_cast<char *>(iov_base), iov_len, tid)
                            : capio_writev_fs(fd, static_cast<char *>(iov_base), iov_len, tid);
    }

    return posix_return_value(write_result, result);
}
#endif // SYS_writev

#endif // CAPIO_POSIX_HANDLERS_WRITE_HPP