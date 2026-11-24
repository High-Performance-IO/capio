#ifndef CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
#define CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP

#if defined(SYS_exit) || defined(SYS_exit_group)

#include "utils/requests.hpp"

/*
 * TODO: adding cleaning of shared memory
 * The process can never interact with the server
 * maybe because is a child process don't need to interact
 * with CAPIO
 */

int exit_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call()");

    // A thread started before libcapio was initialized (e.g. by the constructor of another
    // library, as UCX does with its fuse thread) has no caches and no connection to the server.
    if (write_cache == nullptr || read_cache == nullptr || buf_requests == nullptr) {
        LOG("Thread %d is not a CAPIO thread: nothing to clean up", tid);
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    LOG("Thread %d is a CAPIO thread: clean up", tid);
    read_cache->flush();
    write_cache->flush();
    exit_group_request(tid);

    return CAPIO_POSIX_SYSCALL_SKIP;
}

#if !CAPIO_MEM_STREAM
/**
 * FS variant: at the exit of the process the kernel closes every descriptor, when nothing can
 * announce it. Push the data of each CAPIO descriptor first, with close(dup(fd)) as close does,
 * then announce its close.
 */
inline void capio_fs_close_all(const long tid) {
    START_LOG(tid, "call()");
    for (const int fd : get_capio_fds()) {
        struct stat info{};
        if (syscall_no_intercept(SYS_fstat, fd, &info) == 0 && S_ISDIR(info.st_mode)) {
            continue;
        }
        long error = 0;
        if ((get_capio_fd_flags(fd) & O_ACCMODE) != O_RDONLY) {
            long res = syscall_no_intercept(SYS_dup, fd);
            res      = res >= 0 ? syscall_no_intercept(SYS_close, res)
                                : syscall_no_intercept(SYS_fdatasync, fd); // no descriptor to dup to
            if (res < 0 && res != -EINTR) {
                error = -res;
            }
        }
        close_request_fs(get_capio_fd_path(fd), error, tid);
    }
}

/// @brief Whether the calling thread is the last one of the process
inline bool capio_fs_is_last_thread() {
    struct stat tasks{};
    // a directory has a link for each subdirectory, plus two
    return syscall_no_intercept(SYS_newfstatat, AT_FDCWD, "/proc/self/task", &tasks, 0) == 0 &&
           tasks.st_nlink <= 3;
}

/// @brief The end of a thread closes nothing: only the last one is the exit of the process
int exit_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    if (buf_requests != nullptr && capio_fs_is_last_thread()) {
        capio_fs_close_all(syscall_no_intercept(SYS_gettid));
    }
    return exit_handler(arg0, arg1, arg2, arg3, arg4, arg5, result);
}

int exit_group_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                          long *result) {
    if (buf_requests != nullptr) {
        capio_fs_close_all(syscall_no_intercept(SYS_gettid));
    }
    return exit_handler(arg0, arg1, arg2, arg3, arg4, arg5, result);
}
#endif // !CAPIO_MEM_STREAM

#endif // SYS_exit || SYS_exit_group
#endif // CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
