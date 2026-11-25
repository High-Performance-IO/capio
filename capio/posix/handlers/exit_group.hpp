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

/**
 * FS variant: at the exit of the process the kernel closes every descriptor, after CAPIO can tell
 * the server. Each CAPIO descriptor is flushed first, so that its data is on the file system before
 * the server commits the file, then its close is announced.
 */
inline void capio_close_all_fs(const long tid) {
    START_LOG(tid, "call()");
    for (const int fd : fd_table->fds()) {
        close_request(fd, tid, flush_capio_fd(fd));
    }
}

/// @brief Whether the calling thread is the last one of the process
inline bool capio_is_last_thread_fs() {
    struct stat tasks{};
    // a directory has a link for each subdirectory, plus two
    return syscall_no_intercept(SYS_newfstatat, AT_FDCWD, "/proc/self/task", &tasks, 0) == 0 &&
           tasks.st_nlink <= 3;
}

/// @param whole_process exit_group, or exit of what may be the last thread
inline void capio_exit(const long tid, const bool whole_process) {
    START_LOG(tid, "call(whole_process=%s)", whole_process ? "true" : "false");

    LOG("Thread %d is a CAPIO thread: clean up", tid);
    MEMORY_MODE({
        read_cache->flush();
        write_cache->flush();
    })
    FS_MODE({
        // The end of a thread closes nothing: only the last one is the exit of the process
        if (whole_process || capio_is_last_thread_fs()) {
            capio_close_all_fs(tid);
        }
    })
    exit_group_request(tid);
}

int exit_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    capio_exit(syscall_no_intercept(SYS_gettid), false);
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int exit_group_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                       long *result) {
    capio_exit(syscall_no_intercept(SYS_gettid), true);
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_exit || SYS_exit_group
#endif // CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
