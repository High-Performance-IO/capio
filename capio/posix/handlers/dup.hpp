#ifndef CAPIO_POSIX_HANDLERS_DUP_HPP
#define CAPIO_POSIX_HANDLERS_DUP_HPP

#if defined(SYS_dup) || defined(SYS_dup2) || defined(SYS_dup3)

#include "common/syscall.hpp"
#include "utils/common.hpp"
#include "utils/requests.hpp"

/**
 * FS variant: dup2 and dup3 onto @p fd2. The kernel closes @p fd2 if it is open, and drops the
 * error of that close: a CAPIO descriptor there is flushed first to get it, and its close announced
 * once the call worked.
 */
inline int capio_dup_onto_fs(const long tid, const int fd, const int fd2, const int flags,
                             const bool is_dup3, long *result) {
    START_LOG(tid, "call(fd=%d, fd2=%d, flags=%X)", fd, fd2, flags);
    const bool fd_is_capio  = fd_table->contains(fd);
    const bool fd2_is_capio = fd_table->contains(fd2);
    if (fd == fd2 || (!fd_is_capio && !fd2_is_capio)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    bool replaced = false;
    long error    = 0;
    if (fd2_is_capio && syscall_no_intercept(SYS_fcntl, fd, F_GETFD) >= 0) { // else nothing closes
        replaced = true;
        error    = flush_capio_fd(fd2);
    }
#ifdef SYS_dup2
    *result = is_dup3 ? syscall_no_intercept(SYS_dup3, fd, fd2, flags)
                      : syscall_no_intercept(SYS_dup2, fd, fd2);
#else
    *result = syscall_no_intercept(SYS_dup3, fd, fd2, flags);
#endif
    if (*result < 0) {
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    if (fd2_is_capio) {
        fd_table->remove(fd2);
        if (replaced) {
            close_request(fd2, tid, error);
        }
    }
    if (fd_is_capio) {
        fd_table->dup(fd, fd2, (flags & O_CLOEXEC) == O_CLOEXEC);
        dup_request(fd, fd2, tid);
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

int dup_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    int fd   = static_cast<int>(arg0);

    START_LOG(tid, "call(fd=%d)", fd);

    if (fd_table->contains(fd)) {
        int res;
        MEMORY_MODE({
            res = open("/dev/null", O_WRONLY);
            if (res == -1) {
                *result = -errno;
                return CAPIO_POSIX_SYSCALL_SUCCESS;
            }
        })
        FS_MODE({
            res = static_cast<int>(syscall_no_intercept(SYS_dup, fd));
            if (res < 0) {
                *result = res;
                return CAPIO_POSIX_SYSCALL_SUCCESS;
            }
        })
        fd_table->dup(fd, res, false);
        dup_request(fd, res, tid);

        *result = res;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#ifdef SYS_dup2
int dup2_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    int fd   = static_cast<int>(arg0);
    int fd2  = static_cast<int>(arg1);

    START_LOG(tid, "call(fd=%d, fd2=%d)", fd, fd2);

    FS_MODE({ return capio_dup_onto_fs(tid, fd, fd2, 0, false, result); })
    if (fd_table->contains(fd)) {
        int res = static_cast<int>(syscall_no_intercept(SYS_dup2, fd, fd2));
        if (res == -1) {
            *result = -errno;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        if (fd != res) {
            dup_request(fd, res, tid);
            fd_table->dup(fd, res, false);
        }
        *result = res;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

int dup3_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid  = syscall_no_intercept(SYS_gettid);
    int fd    = static_cast<int>(arg0);
    int fd2   = static_cast<int>(arg1);
    int flags = static_cast<int>(arg2);

    START_LOG(tid, "call(fd=%d, fd2=%d)", fd, fd2);

    FS_MODE({ return capio_dup_onto_fs(tid, fd, fd2, flags, true, result); })
    if (fd_table->contains(fd)) {
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
        dup_request(fd, res, tid);
        fd_table->dup(fd, res, is_cloexec);

        *result = res;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_dup || SYS_dup2 || SYS_dup3
#endif // CAPIO_POSIX_HANDLERS_DUP_HPP
