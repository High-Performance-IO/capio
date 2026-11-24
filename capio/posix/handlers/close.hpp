#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#if defined(SYS_close)

#include "utils/requests.hpp"

int close_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd   = static_cast<int>(arg0);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%ld)", fd);

    if (exists_capio_fd(fd)) {
        write_cache->flush();
        read_cache->flush();
        close_request(fd, tid);
        delete_capio_fd(fd);
        *result = 0;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#if !CAPIO_MEM_STREAM
/**
 * FS variant: the kernel closes first, then CAPIO announces the close. On NFS, close(2) is when the
 * data reaches the server and when a full disk is reported, so the file must not be committed
 * before it returns.
 */
int close_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const int fd   = static_cast<int>(arg0);
    const long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d)", fd);

    if (!exists_capio_fd(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    const std::string path = get_capio_fd_path(fd);
    const bool is_dir      = is_directory(fd);
    // Forget it before closing: another thread may get the same number as soon as it is free
    delete_capio_fd(fd);
    *result = syscall_no_intercept(SYS_close, fd);
    if (*result == -EBADF || is_dir) {
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    // EINTR: Linux has closed it anyway
    close_request_fs(path, *result < 0 && *result != -EINTR ? -*result : 0, tid);
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}
#endif // !CAPIO_MEM_STREAM

#endif // SYS_close
#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP
