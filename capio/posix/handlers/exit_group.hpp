#ifndef CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
#define CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP

#if defined(SYS_exit) || defined(SYS_exit_group)

#include "utils/requests.hpp"
#if !CAPIO_MEM_STREAM
#include "utils/fs_descriptors.hpp"
#endif

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
/// @brief FS variant: the end of a thread closes nothing, only the last one is the exit of the
/// process
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

#endif // SYS_exit || SYS_exit_group
#endif // CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
