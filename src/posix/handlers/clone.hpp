#ifndef CAPIO_POSIX_HANDLERS_CLONE_HPP
#define CAPIO_POSIX_HANDLERS_CLONE_HPP

#include <condition_variable>

#include "globals.hpp"
#include "utils/requests.hpp"

std::mutex clone_mutex;
std::condition_variable clone_cv;
std::set<long> tids;

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
    mtrace_init(child_tid);
    clone_request(parent_tid, child_tid);
    LOG("Child thread %d initialized", child_tid);

    {
        std::lock_guard<std::mutex> lg(clone_mutex);
        tids.insert(child_tid);
    }
    clone_cv.notify_one();
}

#endif // CAPIO_POSIX_HANDLERS_CLONE_HPP
