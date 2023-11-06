#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#include "utils/requests.hpp"

inline int capio_close(int fd, long tid) {
    START_LOG(tid, "call(fd=%ld)", fd);

    if (exists_capio_fd(fd)) {
        close_request(fd, tid);
        delete_capio_fd(fd);
        return 0;
    } else {
        return -2;
    }
}

int close_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd   = static_cast<int>(arg0);
    long tid = syscall_no_intercept(SYS_gettid);

    int res = capio_close(fd, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP
