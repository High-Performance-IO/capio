#ifndef CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
#define CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP

#include "utils/logger.hpp"
#include "utils/requests.hpp"

/*
 * TODO: adding cleaning of shared memory
 * The process can never interact with the server
 * maybe because is a child process don't need to interact
 * with CAPIO
*/

int exit_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    CAPIO_DBG("exit_handler TID[%ld]: add exit_group_request\n", tid);

    exit_group_request(tid);

    CAPIO_DBG("exit_handler TID[%ld]: exit_group_request added\n", tid);

    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_EXIT_GROUP_HPP
