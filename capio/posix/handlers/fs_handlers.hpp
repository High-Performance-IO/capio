#ifndef CAPIO_POSIX_HANDLERS_FS_HANDLERS_HPP
#define CAPIO_POSIX_HANDLERS_FS_HANDLERS_HPP

#if !CAPIO_MEM_STREAM

/*
 * Handlers that the FS variant uses in place of the memory ones (see build_syscall_table). Files
 * are real, so the kernel runs the call: these only wait for data (utils/fs_waiting.hpp), keep
 * track of descriptors and tell the server (utils/fs_descriptors.hpp).
 */

#include "utils/fs_descriptors.hpp"
#include "utils/fs_waiting.hpp"

/* Calls that hand out data: wait for it, then the kernel runs them */

int read_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    const int fd = static_cast<int>(arg0);
    if (exists_capio_fd(fd)) {
        fs_wait_for_data(fd, fs_end_of(fs_read_position(fd, nullptr), arg2),
                         syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int readv_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const int fd = static_cast<int>(arg0);
    if (exists_capio_fd(fd)) {
        const auto *iov = reinterpret_cast<const struct iovec *>(arg1);
        fs_wait_for_data(
            fd,
            fs_end_of(fs_read_position(fd, nullptr), fs_total_length(iov, static_cast<int>(arg2))),
            syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

/* Calls on a descriptor that only wait for the path, like stat */

int fd_consent_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                          long *result) {
    const int fd = static_cast<int>(arg0);
    if (exists_capio_fd(fd)) {
        consent_request_cache->consent_request(get_capio_fd_path(fd),
                                               syscall_no_intercept(SYS_gettid), __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

/* Descriptors: the kernel acts, then CAPIO tracks and announces */

int close_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const int fd = static_cast<int>(arg0);
    if (!exists_capio_fd(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    return fs_close(fd, syscall_no_intercept(SYS_gettid), result);
}

/// @brief The end of a thread closes nothing: only the last one is the exit of the process
int exit_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    const long tid = syscall_no_intercept(SYS_gettid);
    if (fs_is_last_thread()) {
        fs_process_exit(tid);
    }
    exit_group_request(tid);
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int exit_group_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                          long *result) {
    const long tid = syscall_no_intercept(SYS_gettid);
    fs_process_exit(tid);
    exit_group_request(tid);
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // !CAPIO_MEM_STREAM
#endif // CAPIO_POSIX_HANDLERS_FS_HANDLERS_HPP
