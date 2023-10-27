#ifndef CAPIO_POSIX_HANDLERS_FCNTL_HPP
#define CAPIO_POSIX_HANDLERS_FCNTL_HPP

#include "globals.hpp"
#include "utils/requests.hpp"

inline int capio_fcntl(int fd, int cmd, int arg, long tid) {
    START_LOG(tid, "call(fd=%d, cmd=%d, arg=%d)", fd, cmd, arg);

    if (files->find(fd) != files->end()) {
        switch (cmd) {
        case F_GETFD: {
            int res = std::get<3>((*files)[fd]);
            return res;
        }

        case F_SETFD: {
            std::get<3>((*files)[fd]) = arg;
            return 0;
        }

        case F_GETFL: {
            int flags = std::get<2>((*files)[fd]);
            return flags;
        }

        case F_SETFL: {
            std::get<2>((*files)[fd]) = arg;
            return 0;
        }

        case F_DUPFD_CLOEXEC: {
            int dev_fd = open("/dev/null", O_RDONLY);

            if (dev_fd == -1) {
                ERR_EXIT("open /dev/null");
            }

            int res = fcntl(dev_fd, F_DUPFD_CLOEXEC, arg); //
            close(dev_fd);

            (*files)[res] = (*files)[fd];
            std::get<3>((*files)[res]) = FD_CLOEXEC;
            (*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];
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
    auto fd = static_cast<int>(arg0);
    auto cmd = static_cast<int>(arg1);
    auto arg = static_cast<int>(arg2);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, cmd=%d, arg=%d)", fd, cmd, arg);

    int res = capio_fcntl(fd, cmd, arg, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_FCNTL_HPP
