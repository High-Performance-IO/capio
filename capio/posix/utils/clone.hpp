#ifndef CAPIO_POSIX_UTILS_CLONE_HPP
#define CAPIO_POSIX_UTILS_CLONE_HPP
#include <condition_variable>
#include <future>

#include "common/syscall.hpp"
#include "data.hpp"
#include "requests.hpp"


inline std::mutex mutex_child;
inline std::condition_variable child_continue_execution;
inline std::unordered_map<long, std::shared_ptr<std::promise<void>>> child_promises;

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



inline void hook_clone_parent(long child_tid) {
    auto p = std::make_shared<std::promise<void>>();

    {
        std::lock_guard<std::mutex> lg(mutex_child);
        child_promises[child_tid] = p;
    }

    p->set_value();
}

inline void hook_clone_child() {
    const long tid = syscall(SYS_gettid); // raw syscall
    std::shared_ptr<std::promise<void>> p;

    {
        std::unique_lock<std::mutex> ul(mutex_child);
        child_continue_execution.wait(ul, [&]() { return child_promises.count(tid) > 0; });
        p = child_promises[tid];
    }

    p->get_future().wait();

    initialize_new_thread();
}

#endif // CAPIO_POSIX_UTILS_CLONE_HPP
