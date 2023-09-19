#ifndef CAPIO_POSIX_HANDLERS_FORK_HPP
#define CAPIO_POSIX_HANDLERS_FORK_HPP

#include "globals.hpp"
#include "utils/requests.hpp"

inline pid_t capio_fork(long parent_tid) {
    START_LOG(parent_tid, "call()");

    auto pid = static_cast<pid_t>(syscall_no_intercept(SYS_fork));

    if (pid == 0) { // child
        auto child_tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
        mtrace_init(child_tid);
        clone_request(parent_tid, child_tid);
        return 0;
    } else {
        return pid;
    }
}

int fork_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result){
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call()");

    int res = capio_fork(tid);
    *result = (res < 0 ? -errno : res);
    return 0;
}

#endif // CAPIO_POSIX_HANDLERS_FORK_HPP
