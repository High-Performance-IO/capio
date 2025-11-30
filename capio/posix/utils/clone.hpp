#ifndef CAPIO_POSIX_UTILS_CLONE_HPP
#define CAPIO_POSIX_UTILS_CLONE_HPP

#include <condition_variable>
#include <unordered_set>

#include "common/syscall.hpp"

#include "data.hpp"
#include "requests.hpp"

inline std::condition_variable clone_cv;

inline void initialize_cloned_thread() {
    const long tid = syscall_no_intercept(SYS_gettid);
    const long pid = syscall_no_intercept(SYS_getpid);
    const long parent_tid = syscall_no_intercept(SYS_getppid);


    syscall_no_intercept_flag = true;
    const auto capio_app_name = get_capio_app_name();
    syscall_no_intercept_flag = false;

    START_LOG(tid, "call()");

    init_client();
    register_data_listener(tid);
    handshake_named_request(tid, pid, capio_app_name);
    LOG("Starting child thread %d", tid);
}

inline void hook_clone_parent(const long child_tid) {
    const auto parent_tid = syscall_no_intercept(SYS_gettid);
    START_LOG(parent_tid, "call(parent_tid=%d, child_tid=%d)", parent_tid, child_tid);
    clone_request(parent_tid, child_tid);
}

#endif // CAPIO_POSIX_UTILS_CLONE_HPP
