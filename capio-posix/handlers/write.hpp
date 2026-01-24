#ifndef CAPIO_POSIX_HANDLERS_WRITE_HPP
#define CAPIO_POSIX_HANDLERS_WRITE_HPP

#include "utils/common.hpp"
#include "utils/requests.hpp"

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

    if (!store_file_in_memory(get_capio_fd_path(fd), tid)) {
        LOG("File is to be handled in FS. skipping write");
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    auto write_result = capio_write_mem(fd, buffer, count, tid);

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
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    if (!store_file_in_memory(get_capio_fd_path(fd), tid)) {
        LOG("File is to be handled in FS. skipping write");
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
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
        write_result += capio_write_mem(fd, static_cast<char *>(iov_base), iov_len, tid);
    }

    return posix_return_value(write_result, result);
}
#endif // SYS_writev

#endif // CAPIO_POSIX_HANDLERS_WRITE_HPP