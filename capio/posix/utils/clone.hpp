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

inline void hook_clone_parent(long child_tid) {
    // syscall_intercept invokes this hook even when the clone fails (e.g. clone3 returning
    // -ENOSYS under seccomp filters, after which glibc falls back to clone): do not register
    // a nonexistent child with the server.
    if (child_tid <= 0) {
        return;
    }
    const auto tid = syscall_no_intercept(SYS_gettid);
    clone_request(tid, child_tid);
}

/**
 * Called in the child after a successful clone/clone3. When the child is a new thread, wait for
 * the server to acknowledge the handshake. When it is a new process (fork semantics, i.e. no
 * CLONE_THREAD, as issued by fork() on architectures without SYS_fork such as aarch64), behave
 * like fork_handler and do not wait: the child may be the one that is about to exec the CAPIO
 * server, so waiting for a reply would deadlock.
 */
inline void hook_clone_child() {
    const long pid = syscall_no_intercept(SYS_getpid);
    const long tid = syscall_no_intercept(SYS_gettid);
    initialize_new_thread(pid != tid);
}

#endif // CAPIO_POSIX_UTILS_CLONE_HPP
