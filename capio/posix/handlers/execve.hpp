#ifndef CAPIO_POSIX_HANDLERS_EXECVE_HPP
#define CAPIO_POSIX_HANDLERS_EXECVE_HPP

#if defined(SYS_execve)

#include "utils/snapshot.hpp"

int execve_handler(pid_t tid, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                   long *result) {
    START_LOG(tid, "call()");

    create_snapshot(tid);

    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_execve
#endif // CAPIO_POSIX_HANDLERS_EXECVE_HPP
