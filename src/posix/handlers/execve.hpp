#ifndef CAPIO_POSIX_HANDLERS_EXECVE_HPP
#define CAPIO_POSIX_HANDLERS_EXECVE_HPP

#include "utils/snapshot.hpp"

int execve_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call()");

    create_snapshot(tid);

    return POSIX_SYSCALL_SKIP;
}

#endif // CAPIO_POSIX_HANDLERS_EXECVE_HPP
