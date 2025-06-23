#ifndef CAPIO_POSIX_UTILS_CLONE_HPP
#define CAPIO_POSIX_UTILS_CLONE_HPP

#include <condition_variable>
#include <unordered_set>

#include "capio/syscall.hpp"

#include "requests.hpp"

inline std::mutex clone_mutex;
inline std::condition_variable clone_cv;
inline std::unordered_set<pid_t> *tids;

inline bool clone_after_null_child_stack = false;

inline bool is_capio_tid(const pid_t tid) {
    const std::lock_guard<std::mutex> lg(clone_mutex);
    return tids->find(tid) != tids->end();
}

inline void register_capio_tid(const pid_t tid) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(tid=%ld)", tid);
    const std::lock_guard<std::mutex> lg(clone_mutex);
    tids->insert(tid);
    LOG("Pid inserted ? %s", tids->find(tid) == tids->end() ? "NO" : "YES");
}

inline void remove_capio_tid(const pid_t tid) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(tid=%ld)", tid);
    const std::lock_guard<std::mutex> lg(clone_mutex);
    if (tids->find(tid) != tids->end()) {
        tids->erase(tid);
    }
}

inline void init_threading_support() { tids = new std::unordered_set<pid_t>{}; }

inline void init_process(pid_t tid) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(tid=%ld)", tid);

    syscall_no_intercept_flag = true;

    auto *p_buf_response = new ResponseQueue(SHM_COMM_CHAN_NAME_RESP + std::to_string(tid));
    LOG("Created buf response");
    bufs_response->insert({tid, p_buf_response});
    LOG("Created request response buffer with name: %s",
        (SHM_COMM_CHAN_NAME_RESP + std::to_string(tid)).c_str());

    const char *capio_app_name = get_capio_app_name();
    auto pid                   = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    LOG("sending handshake with tid=%ld, pid=%ld", tid, pid);

    /**
     * The previous if, for an anonymous handshake was present, however the get_capio_app_name()
     * never returns a nullptr, as there is a default name, thus rendering the
     * handshake_anonymous_request() useless
     */
    handshake_request(tid, pid, capio_app_name);

    syscall_no_intercept_flag = false;
}

inline void hook_clone_child() {
    auto tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    /*
     * This piece of code is aimed at addressing issues with applications that spawn several
     * thousand threads that only do computations. When this occurs, under some circumstances CAPIO
     * might fail to allocate shared memory objects. As such, if child threads ONLY do computation,
     * we can safely ignore them with CAPIO.
     */
    syscall_no_intercept_flag     = true;
    thread_local char *skip_child = std::getenv("CAPIO_IGNORE_CHILD_THREADS");
    if (skip_child != nullptr) {
        auto skip_child_str = std::string(skip_child);
        if (skip_child_str == "ON" || skip_child_str == "TRUE" || skip_child_str == "YES") {
            return;
        }
    }
    syscall_no_intercept_flag = false;

    if (!clone_after_null_child_stack) {
        syscall_no_intercept_flag = true;
        std::unique_lock<std::mutex> lock(clone_mutex);
        clone_cv.wait(lock, [&tid] { return tids->find(tid) != tids->end(); });
        /**
         * Freeing memory here through `tids.erase()` can cause a SIGSEGV error
         * in the libc, which tries to load the `__ctype_b_loc` table but fails
         * because it is not initialized yet. For this reason, a thread's `tid`
         * is removed from the `tids` set only when the thread terminates.
         */
        lock.unlock();
        syscall_no_intercept_flag = false;
    } else {
        /*
         * Needed to enable logging when SYS_clone is called with child_stack==NULL.
         * In this case, the thread_local variables are initialized and not set to a nullptr.
         * For this reason, we reset them here
         */

#ifdef CAPIO_LOG
        logfileOpen = false;
        logfileFD   = -1;
        bzero(logfile_path, PATH_MAX);
#endif
        // We cannot perform delete, as it will destroy also shm objects. put ptr to nullptr
        // and accept a small memory leak
        bufs_response            = nullptr;
        buf_requests             = nullptr;
        stc_queue                = nullptr;
        cts_queue                = nullptr;
        write_request_cache_fs   = nullptr;
        read_request_cache_fs    = nullptr;
        consent_request_cache_fs = nullptr;
        write_request_cache_mem  = nullptr;
        read_request_cache_mem   = nullptr;
        init_client();
        clone_after_null_child_stack = false;
    }

    START_SYSCALL_LOGGING();
    START_LOG(tid, "call()");
    LOG("Initializing child thread %d", tid);
    init_process(tid);
    LOG("Child thread %d initialized", tid);
    LOG("Starting child thread %d", tid);
    init_caches();
}

inline void hook_clone_parent(long child_tid) {
    SUSPEND_SYSCALL_LOGGING();
    auto parent_tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    START_LOG(parent_tid, "call(parent_tid=%d, child_tid=%d)", parent_tid, child_tid);

    if (child_tid < 0) {
        LOG("Skipping clone as child tid is set to %d: %s", child_tid, std::strerror(child_tid));
        return;
    }

    clone_after_null_child_stack = false;

    register_capio_tid(child_tid);
    clone_cv.notify_all();
}

#endif // CAPIO_POSIX_UTILS_CLONE_HPP