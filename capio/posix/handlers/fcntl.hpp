#ifndef CAPIO_POSIX_HANDLERS_FCNTL_HPP
#define CAPIO_POSIX_HANDLERS_FCNTL_HPP

#if defined(SYS_fcntl)

#include "utils/requests.hpp"

int fcntl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd  = static_cast<int>(arg0);
    auto cmd = static_cast<int>(arg1);
    auto arg = static_cast<int>(arg2);
    long tid = syscall_no_intercept(SYS_gettid);

    // int res = capio_fcntl(fd, cmd, arg, tid);

    START_LOG(tid, "call(fd=%d, cmd=%d, arg=%d)", fd, cmd, arg);

    if (exists_capio_fd(fd)) {
        switch (cmd) {
        case F_GETFD: {
            *result = get_capio_fd_cloexec(fd);
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }

        case F_SETFD: {
            set_capio_fd_cloexec(fd, arg);
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }

        case F_GETFL: {
            *result = get_capio_fd_flags(fd);
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }

        case F_SETFL: {
            set_capio_fd_flags(fd, arg);
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }

        case F_DUPFD_CLOEXEC: {
            int dev_fd = open("/dev/null", O_RDONLY);

            if (dev_fd == -1) {
                ERR_EXIT("open /dev/null");
            }

            int res = fcntl(dev_fd, F_DUPFD_CLOEXEC, arg);
            if (res == -1) {
                *result = -errno;
            }
            close(dev_fd);
            dup_capio_fd(tid, fd, res, true);
            dup_request(fd, res, tid);

            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }

        default:
            ERR_EXIT("fcntl with cmd %d is not yet supported", cmd);
        }
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_fcntl
#endif // CAPIO_POSIX_HANDLERS_FCNTL_HPP
