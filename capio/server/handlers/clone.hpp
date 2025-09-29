#ifndef CAPIO_SERVER_HANDLERS_CLONE_HPP
#define CAPIO_SERVER_HANDLERS_CLONE_HPP

#include "common.hpp"

// TODO: caching info
inline void handle_clone(pid_t parent_tid, pid_t child_tid) {
    START_LOG(gettid(), "call(parent_tid=%d, child_tid=%d)", parent_tid, child_tid);
    init_process(child_tid);
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
}

#endif // CAPIO_SERVER_HANDLERS_CLONE_HPP
