#ifndef CAPIO_POSIX_UTILS_CLONE_HPP
#define CAPIO_POSIX_UTILS_CLONE_HPP

#include "calf/SyscallLogger.h"
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
    SET_CALF_SYSCALL_HANDLER(syscall_no_intercept);

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

/**
 * Set by the hook when a clone or clone3 with CLONE_VFORK starts, as glibc's posix_spawn and
 * system() issue.
 * Such a child shares the parent's memory, and the parent stays suspended until the child calls
 * execve or exits. Initializing the child would wait for the server to acknowledge the parent's
 * clone_request, which the parent only sends once it resumes: both would wait forever. The child
 * therefore leaves CAPIO alone until execve, whose new image initializes CAPIO in its constructor.
 * It is not thread_local, because the child runs on the parent's thread-local storage.
 */
inline bool vfork_in_progress = false;

inline void hook_clone_parent(long child_tid) {
    if (vfork_in_progress) {
        // The child has called execve or exited: resume interception in the parent.
        vfork_in_progress         = false;
        syscall_no_intercept_flag = false;
        return;
    }
    const auto tid = syscall_no_intercept(SYS_gettid);
    clone_request(tid, child_tid);
}

inline void hook_clone_child() {
    if (vfork_in_progress) {
        // Shared with the parent: stop intercepting until execve replaces this image.
        syscall_no_intercept_flag = true;
        return;
    }
    initialize_new_thread(true);
}

#endif // CAPIO_POSIX_UTILS_CLONE_HPP
