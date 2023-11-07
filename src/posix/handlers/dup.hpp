#ifndef CAPIO_POSIX_HANDLERS_DUP_HPP
#define CAPIO_POSIX_HANDLERS_DUP_HPP

#include "capio/syscall.hpp"

#include "utils/requests.hpp"

inline int capio_dup(int fd, long tid) {
    START_LOG(tid, "call(fd=%d)", fd);

    if (exists_capio_fd(fd)) {
        int res = open("/dev/null", O_WRONLY);
        if (res == -1) {
            ERR_EXIT("open in capio_dup");
        }
        dup_request(fd, res, tid);
        dup_capio_fd(tid, fd, res);

        return res;
    } else {
        return -2;
    }
}

inline int capio_dup2(int fd, int fd2, long tid) {
    START_LOG(tid, "call(fd=%d, fd2=%d)", fd, fd2);

    if (exists_capio_fd(fd)) {
        int res = static_cast<int>(syscall_no_intercept(SYS_dup2, fd, fd2));
        if (res == -1) {
            return -1;
        }
        dup_request(fd, res, tid);
        dup_capio_fd(tid, fd, res);
        return res;
    } else {
        return -2;
    }
}

int dup_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    int fd   = static_cast<int>(arg0);

    int res = capio_dup(fd, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int dup2_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    int fd   = static_cast<int>(arg0);
    int fd2  = static_cast<int>(arg1);

    int res = capio_dup2(fd, fd2, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_DUP_HPP
