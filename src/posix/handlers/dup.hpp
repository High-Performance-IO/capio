#ifndef CAPIO_POSIX_HANDLERS_DUP_HPP
#define CAPIO_POSIX_HANDLERS_DUP_HPP

#if defined(SYS_dup) || defined(SYS_dup2) || defined(SYS_dup3)

#include "capio/syscall.hpp"
#include "utils/requests.hpp"

int dup_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    int fd   = static_cast<int>(arg0);

    START_LOG(tid, "call(fd=%d)", fd);

    if (exists_capio_fd(fd)) {
        auto res = static_cast<int>(syscall_no_intercept(SYS_dup, fd));
        if (res == -1) {
            *result = -errno;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        dup_capio_fd(tid, fd, res, false);

        *result = res;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int dup2_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    int fd   = static_cast<int>(arg0);
    int fd2  = static_cast<int>(arg1);

    START_LOG(tid, "call(fd=%d, fd2=%d)", fd, fd2);

    if (exists_capio_fd(fd)) {
        int res = static_cast<int>(syscall_no_intercept(SYS_dup2, fd, fd2));
        if (res == -1) {
            *result = -errno;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        if (fd != res) {
            dup_capio_fd(tid, fd, res, false);
        }
        *result = res;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int dup3_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid  = syscall_no_intercept(SYS_gettid);
    int fd    = static_cast<int>(arg0);
    int fd2   = static_cast<int>(arg1);
    int flags = static_cast<int>(arg2);

    //  int res = capio_dup3(fd, fd2, flags, tid);

    START_LOG(tid, "call(fd=%d, fd2=%d)", fd, fd2);

    if (exists_capio_fd(fd)) {
        if (fd == fd2) {
            errno   = EINVAL;
            *result = -errno;
            return 0;
        }
        int res = static_cast<int>(syscall_no_intercept(SYS_dup3, fd, fd2, flags));
        if (res == -1) {
            *result = -errno;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        bool is_cloexec = (flags & O_CLOEXEC) == O_CLOEXEC;
        dup_capio_fd(tid, fd, res, is_cloexec);

        *result = res;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_dup || SYS_dup2 || SYS_dup3
#endif // CAPIO_POSIX_HANDLERS_DUP_HPP
