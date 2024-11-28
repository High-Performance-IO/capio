#ifndef CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
#define CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP

#if defined(SYS_exit) || defined(SYS_exit_group)

#include "capio/logger.hpp"

#include "utils/requests.hpp"

/*
 * TODO: adding cleaning of shared memory
 * The process can never interact with the server
 * maybe because is a child process don't need to interact
 * with CAPIO
 */

int exit_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    START_LOG(tid, "call()");

    if (is_capio_tid(tid)) {
        LOG("Thread %d is a CAPIO thread: clean up", tid);
        exit_group_request(tid);
        remove_capio_tid(tid);
    }

    if (const auto itm = bufs_response->find(tid); itm != bufs_response->end()) {
        delete itm->second;
        bufs_response->erase(tid);
        LOG("Removed response buffer");
    }

    delete_caches();
    LOG("Removed caches");

    delete stc_queue;
    delete cts_queue;
    LOG("Removed data queues");

    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_exit || SYS_exit_group
#endif // CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
