#ifndef CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
#define CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP

#include "capio/logger.hpp"

#include "utils/requests.hpp"

/*
 * TODO: adding cleaning of shared memory
 * The process can never interact with the server
 * maybe because is a child process don't need to interact
 * with CAPIO
 */

int exit_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call()");

    exit_group_request(tid);

    return POSIX_SYSCALL_SKIP;
}

#endif // CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
