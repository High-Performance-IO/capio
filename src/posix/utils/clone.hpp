#ifndef CAPIO_POSIX_UTILS_CLONE_HPP
#define CAPIO_POSIX_UTILS_CLONE_HPP

#include <condition_variable>
#include <unordered_set>

#include "capio/syscall.hpp"

#include "requests.hpp"

inline std::mutex clone_mutex;
inline std::condition_variable clone_cv;
inline std::unordered_set<pid_t> *tids;

inline bool is_capio_tid(const pid_t tid) {
    const std::lock_guard<std::mutex> lg(clone_mutex);
    return tids->find(tid) != tids->end();
}

inline void register_capio_tid(const pid_t tid) {
    const std::lock_guard<std::mutex> lg(clone_mutex);
    tids->insert(tid);
}

inline void remove_capio_tid(const pid_t tid) {
    const std::lock_guard<std::mutex> lg(clone_mutex);
    tids->erase(tid);
}

void init_threading_support() { tids = new std::unordered_set<pid_t>{}; }

void init_process(pid_t tid) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(tid=%ld)", tid);

    syscall_no_intercept_flag = true;

    register_listener(tid);

    const char *capio_app_name = get_capio_app_name();
    auto pid                   = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    if (capio_app_name == nullptr) {
        handshake_anonymous_request(tid, pid);
    } else {
        handshake_named_request(tid, pid, capio_app_name);
    }

    syscall_no_intercept_flag = false;
}

void hook_clone_child() {
    auto tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    START_LOG(tid, "call()");

    std::unique_lock<std::mutex> lock(clone_mutex);
    LOG("Waiting initialization from parent thread");
    clone_cv.wait(lock, [&tid] { return tids->find(tid) != tids->end(); });

    /**
     * Freeing memory here through `tids.erase()` can cause a SIGSEGV error
     * in the libc, which tries to load the `__ctype_b_loc` table but fails
     * because it is not initialized yet. For this reason, a thread's `tid`
     * is removed from the `tids` set only when the thread terminates.
     */

    lock.unlock();
    LOG("Starting child thread %d", tid);
    write_request_cache = new WriteRequestCache(8192);
    read_request_cache  = new ReadRequestCache();
}

void hook_clone_parent(long child_tid) {
    SUSPEND_SYSCALL_LOGGING();
    auto parent_tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    START_LOG(parent_tid, "call(parent_tid=%d, child_tid=%d)", parent_tid, child_tid);

    if (child_tid < 0) {
        LOG("Skipping clone as child tid is set to %d: %s", child_tid, std::strerror(child_tid));
        return;
    }

    LOG("Initializing child thread %d", child_tid);
    init_process(child_tid);
    LOG("Child thread %d initialized", child_tid);

    register_capio_tid(child_tid);
    clone_cv.notify_all();
}

#endif // CAPIO_POSIX_UTILS_CLONE_HPP
