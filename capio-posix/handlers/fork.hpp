#ifndef CAPIO_POSIX_HANDLERS_FORK_HPP
#define CAPIO_POSIX_HANDLERS_FORK_HPP

#if defined(SYS_fork)

#include "utils/clone.hpp"
#include "utils/requests.hpp"

inline int fork_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                        long *result, const pid_t tid) {
    long parent_tid = tid;
    auto pid        = static_cast<pid_t>(syscall_no_intercept(SYS_fork));

    START_LOG(parent_tid, "call(pid=%ld)", pid);

    if (pid == 0) {
        // child
        capio_current_thread_id = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
        init_process(capio_current_thread_id);
        *result = 0;
        return posix_return_value(0, result);
    }

    return posix_return_value(pid, result);
}

#endif // SYS_fork
#endif // CAPIO_POSIX_HANDLERS_FORK_HPP