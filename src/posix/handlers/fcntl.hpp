#ifndef CAPIO_POSIX_HANDLERS_FCNTL_HPP
#define CAPIO_POSIX_HANDLERS_FCNTL_HPP

#include "utils/requests.hpp"

inline int capio_fcntl(int fd, int cmd, int arg, long tid) {
    START_LOG(tid, "call(fd=%d, cmd=%d, arg=%d)", fd, cmd, arg);

    if (exists_capio_fd(fd)) {
        switch (cmd) {
        case F_GETFD: {
            return get_capio_fd_cloexec(fd);
        }

        case F_SETFD: {
            set_capio_fd_cloexec(fd, arg);
            return 0;
        }

        case F_GETFL: {
            return get_capio_fd_flags(fd);
        }

        case F_SETFL: {
            set_capio_fd_flags(fd, arg);
            return 0;
        }

        case F_DUPFD_CLOEXEC: {
            int dev_fd = open("/dev/null", O_RDONLY);

            if (dev_fd == -1) {
                ERR_EXIT("open /dev/null");
            }

            int res = fcntl(dev_fd, F_DUPFD_CLOEXEC, arg);
            close(dev_fd);
            dup_capio_fd(tid, fd, res, true);
            dup_request(fd, res, tid);

            return res;
        }

        default:
            ERR_EXIT("fcntl with cmd %d is not yet supported", cmd);
        }
    } else {
        return -2;
    }
}

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
            return 0;
        }

        case F_SETFD: {
            set_capio_fd_cloexec(fd, arg);
            return 0;
        }

        case F_GETFL: {
            *result = get_capio_fd_flags(fd);
            return 0;
        }

        case F_SETFL: {
            set_capio_fd_flags(fd, arg);
            return 0;
        }

        case F_DUPFD_CLOEXEC: {
            int dev_fd = open("/dev/null", O_RDONLY);

            if (dev_fd == -1) {
                ERR_EXIT("open /dev/null");
            }

            int res = fcntl(dev_fd, F_DUPFD_CLOEXEC, arg);
            close(dev_fd);
            dup_capio_fd(tid, fd, res, true);
            dup_request(fd, res, tid);

            if (res == -1) {
                *result = -errno;
            }
            return 0;
        }

        default:
            ERR_EXIT("fcntl with cmd %d is not yet supported", cmd);
        }
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_FCNTL_HPP
