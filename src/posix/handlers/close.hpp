#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#include "utils/requests.hpp"

int close_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd   = static_cast<int>(arg0);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%ld)", fd);

    if (exists_capio_fd(fd)) {
        close_request(fd, tid);
        delete_capio_fd(fd);
        *result = 0;
    }
    return POSIX_SYSCALL_SKIP;
}

#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP
