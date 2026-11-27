#ifndef CAPIO_POSIX_HANDLERS_DUP_HPP
#define CAPIO_POSIX_HANDLERS_DUP_HPP

#if defined(SYS_dup) || defined(SYS_dup2) || defined(SYS_dup3)

#include "common/syscall.hpp"
#include "utils/common.hpp"
#include "utils/requests.hpp"
#include "utils/tid.hpp"

/**
 * dup2 and dup3: make @p fd2 a copy of @p fd. If @p fd2 is open, the kernel closes it first,
 * without a close(2) that CAPIO sees, and drops the error of that close. So a CAPIO descriptor
 * there is flushed first, as close does, and its close is told to the server once the call worked.
 */
inline int capio_dup_onto(const long tid, const int fd, const int fd2, const int flags,
                          const bool is_dup3, long *result) {
    START_LOG(tid, "call(fd=%d, fd2=%d, flags=%X)", fd, fd2, flags);
    const bool fd_is_capio  = fd_table->contains(fd);
    const bool fd2_is_capio = fd_table->contains(fd2);
    if (fd == fd2 || (!fd_is_capio && !fd2_is_capio)) {
        return CAPIO_POSIX_SYSCALL_SKIP; // fd == fd2: the kernel neither copies nor closes
    }
    bool replaced = false;
    long error    = 0;
    if (fd2_is_capio && syscall_no_intercept(SYS_fcntl, fd, F_GETFD) >= 0) { // else nothing closes
        replaced = true;
        MEMORY_MODE({
            write_cache->flush();
            read_cache->flush();
        })
        FS_MODE({ error = flush_capio_fd(fd2); })
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
    long tid = capio_tid();
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
    const long tid = capio_tid();
    return capio_dup_onto(tid, static_cast<int>(arg0), static_cast<int>(arg1), 0, false, result);
}
#endif

int dup3_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const long tid = capio_tid();
    return capio_dup_onto(tid, static_cast<int>(arg0), static_cast<int>(arg1),
                          static_cast<int>(arg2), true, result);
}

#endif // SYS_dup || SYS_dup2 || SYS_dup3
#endif // CAPIO_POSIX_HANDLERS_DUP_HPP
