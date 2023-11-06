#ifndef CAPIO_POSIX_UTILS_CLONE_HPP
#define CAPIO_POSIX_UTILS_CLONE_HPP

#include <condition_variable>
#include <unordered_set>

#include "capio/syscall.hpp"

#include "data.hpp"
#include "requests.hpp"

std::mutex clone_mutex;
std::condition_variable clone_cv;
std::unordered_set<long> tids;

void init_process(long tid) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(tid=%ld)", tid);

    syscall_no_intercept_flag = true;

    register_listener(tid);
    register_data_listener(tid);

    const char *capio_app_name = get_capio_app_name();
    long pid                   = syscall_no_intercept(SYS_getpid);
    if (capio_app_name == nullptr) {
        handshake_anonymous_request(tid, pid);
    } else {
        handshake_named_request(tid, pid, capio_app_name);
    }

    syscall_no_intercept_flag = false;
}

void hook_clone_child() {
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call()");

    std::unique_lock<std::mutex> lock(clone_mutex);
    LOG("Waiting initialization from parent thread");
    clone_cv.wait(lock, [&tid] { return tids.find(tid) != tids.end(); });
    tids.erase(tid);
    lock.unlock();
    LOG("Starting child thread %d", tid);
}

void hook_clone_parent(long child_tid) {
    long parent_tid = syscall_no_intercept(SYS_gettid);
    START_LOG(parent_tid, "call(parent_tid=%d, child_tid=%ld)", parent_tid, child_tid);

    LOG("Initializing child thread %d", child_tid);
    init_process(child_tid);
    clone_request(parent_tid, child_tid);
    LOG("Child thread %d initialized", child_tid);

    {
        std::lock_guard<std::mutex> lg(clone_mutex);
        tids.insert(child_tid);
    }
    clone_cv.notify_one();
}

#endif // CAPIO_POSIX_UTILS_CLONE_HPP
