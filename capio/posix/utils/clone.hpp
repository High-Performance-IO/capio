#ifndef CAPIO_POSIX_UTILS_CLONE_HPP
#define CAPIO_POSIX_UTILS_CLONE_HPP
#include <condition_variable>

#include "common/syscall.hpp"
#include "data.hpp"
#include "requests.hpp"

inline std::mutex mutex_child;
inline std::condition_variable child_continue_execution;
inline std::vector<long> initialized_children;

/**
 * Initialize the required data structures for the new child thread, and then proceed to execute a
 * handshake request, using the DEFAULT CAPIO APP NAME is no app name is provided.
 */
inline void initialize_new_thread() {
    const long tid = syscall_no_intercept(SYS_gettid);
    const long pid = syscall_no_intercept(SYS_getpid);

    syscall_no_intercept_flag = true;
    const auto capio_app_name = get_capio_app_name();
    syscall_no_intercept_flag = false;

    START_LOG(tid, "call(tid=%d, pid=%d, app_name=%s)", tid, pid, capio_app_name);

    initialize_data_queues(tid);
    init_client(tid);

    handshake_named_request(tid, pid, capio_app_name);
    LOG("Starting child thread %d", tid);
}

/**
 * Entry point after a SYS_clone occurs. This function wraps around the initialize_new_thread()
 * routine to allow the hook_clone_parent() routine to issue a clone_request before allowing the
 * child to execute. This ensures that before processing the handshake from the child, the
 * capio_server has cloned all the file descriptors from the parent to the children.
 */
inline void hook_clone_child() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    std::unique_lock ul(mutex_child);
    child_continue_execution.wait(ul, []() {
        const long tid = syscall_no_intercept(SYS_gettid);
        return std::find(initialized_children.begin(), initialized_children.end(), tid) !=
               initialized_children.end();
    });
    LOG("Parent unlocked thread");
    initialize_new_thread();
}

/**
 * Routine given to syscall_intercept to handle the execution of a clone syscall. After issuing a
 * clone request, it increases by one the sem_post semaphore unloking the children thread.
 * @param child_pid
 */
inline void hook_clone_parent(const long child_pid) {
    const auto parent_tid = syscall_no_intercept(SYS_gettid);
    START_LOG(parent_tid, "call(parent_tid=%d, child_pid=%d)", parent_tid, child_pid);
    clone_request(parent_tid, child_pid);

    {
        std::lock_guard lg(mutex_child);
        initialized_children.push_back(child_pid);
    }

    child_continue_execution.notify_all();
}

#endif // CAPIO_POSIX_UTILS_CLONE_HPP
