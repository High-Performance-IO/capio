#ifndef CAPIO_SERVER_HANDLERS_CLONE_HPP
#define CAPIO_SERVER_HANDLERS_CLONE_HPP

#include "handlers/common.hpp"

// TODO: move variables within the client_manager class when available
inline std::mutex mutex_thread_allowed_to_continue;
inline std::vector<int> thread_allowed_to_continue;
inline std::condition_variable cv_thread_allowed_to_continue;

// TODO: caching info
inline void handle_clone(pid_t parent_tid, pid_t child_tid) {
    START_LOG(gettid(), "call(parent_tid=%d, child_tid=%d)", parent_tid, child_tid);
    clone_capio_file(parent_tid, child_tid);
    int ppid    = pids[parent_tid];
    int new_pid = pids[child_tid];
    if (ppid != new_pid) {
        writers[child_tid] = writers[parent_tid];
        for (auto &p : writers[child_tid]) {
            p.second = false;
        }
    }
}

void clone_handler(const char *const str) {
    pid_t parent_tid, child_tid;
    sscanf(str, "%d %d", &parent_tid, &child_tid);
    handle_clone(parent_tid, child_tid);
    {
        std::lock_guard lock(mutex_thread_allowed_to_continue);
        thread_allowed_to_continue.push_back(child_tid);
        cv_thread_allowed_to_continue.notify_all();
    }
}

#endif // CAPIO_SERVER_HANDLERS_CLONE_HPP
