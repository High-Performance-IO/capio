#ifndef CAPIO_POSIX_HANDLERS_FORK_HPP
#define CAPIO_POSIX_HANDLERS_FORK_HPP

#if defined(SYS_fork)

#include "utils/clone.hpp"
#include "utils/requests.hpp"

int fork_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto pid        = static_cast<pid_t>(syscall_no_intercept(SYS_fork));
    long parent_tid = syscall_no_intercept(SYS_gettid);
    START_LOG(parent_tid, "call(pid=%ld)", pid);

    if (pid == 0) { // child
        // TODO: check if this is correct
        initialize_cloned_thread();
        *result = 0;
    } else {
        *result = pid;
    }

    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // SYS_fork
#endif // CAPIO_POSIX_HANDLERS_FORK_HPP
