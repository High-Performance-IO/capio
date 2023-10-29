#ifndef CAPIO_POSIX_HANDLERS_EXECVE_HPP
#define CAPIO_POSIX_HANDLERS_EXECVE_HPP

#include "globals.hpp"
#include "utils/snapshot.hpp"

int execve_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call()");

    create_snapshot(files, capio_files_descriptors, tid);

    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_EXECVE_HPP