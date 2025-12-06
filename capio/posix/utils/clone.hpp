#ifndef CAPIO_POSIX_UTILS_CLONE_HPP
#define CAPIO_POSIX_UTILS_CLONE_HPP

#include "common/syscall.hpp"
#include "data.hpp"
#include "requests.hpp"

/**
 * Initialize the required data structures for the new child thread, and then proceed to execute a
 * handshake request, using the DEFAULT CAPIO APP NAME is no app name is provided.
 * The wait parameter is used to await for an explicit reply from the server that this client
 * has been successfully initialized on the server side, whenever the initialize_new_thread routine
 * is called after a SYS_clone is issued.
 */
inline void initialize_new_thread(const bool wait = false) {
    const long tid = syscall_no_intercept(SYS_gettid);
    const long pid = syscall_no_intercept(SYS_getpid);

    syscall_no_intercept_flag = true;
    const auto capio_app_name = get_capio_app_name();
    syscall_no_intercept_flag = false;

    START_LOG(tid, "call(tid=%d, pid=%d, app_name=%s)", tid, pid, capio_app_name);

    initialize_data_queues(tid);
    init_client(tid);

    handshake_named_request(tid, pid, capio_app_name, wait);
    LOG("Starting child thread %d", tid);
}

inline void hook_clone_parent(long child_tid) {
    const auto tid = syscall_no_intercept(SYS_gettid);
    clone_request(tid, child_tid);
}

inline void hook_clone_child() { initialize_new_thread(true); }

#endif // CAPIO_POSIX_UTILS_CLONE_HPP
