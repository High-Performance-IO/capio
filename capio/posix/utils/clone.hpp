#ifndef CAPIO_POSIX_UTILS_CLONE_HPP
#define CAPIO_POSIX_UTILS_CLONE_HPP
#include <condition_variable>

#include "common/syscall.hpp"
#include "data.hpp"
#include "requests.hpp"

inline std::mutex clone_mutex;
inline std::condition_variable clone_cv;

inline void initialize_cloned_thread() {
    const long tid = syscall_no_intercept(SYS_gettid);
    const long pid = syscall_no_intercept(SYS_getpid);

    syscall_no_intercept_flag = true;
    const auto capio_app_name = get_capio_app_name();
    syscall_no_intercept_flag = false;

    START_LOG(tid, "call()");

    initialize_data_queues(tid);
    init_client(tid);

    handshake_named_request(tid, pid, capio_app_name);
    LOG("Starting child thread %d", tid);
}

inline void hook_clone_child() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    std::unique_lock lock(clone_mutex);
    clone_cv.wait(lock);
    LOG("Parent unlocked thread");
    initialize_cloned_thread();
}

inline void hook_clone_parent(const long child_pid) {
    const auto parent_tid = syscall_no_intercept(SYS_gettid);
    START_LOG(parent_tid, "call(parent_tid=%d, child_pid=%d)", parent_tid, child_pid);
    clone_request(parent_tid, child_pid);
    clone_cv.notify_all();
}

#endif // CAPIO_POSIX_UTILS_CLONE_HPP
