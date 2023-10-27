#ifndef CAPIO_POSIX_HANDLERS_IOCTL_HPP
#define CAPIO_POSIX_HANDLERS_IOCTL_HPP

#include "globals.hpp"

inline int capio_ioctl(int fd, unsigned long request, long tid) {
    START_LOG(tid, "call(fd=%d, request=%ld)", fd, request, tid);

    if (files->find(fd) != files->end()) {
        errno = ENOTTY;
        return -1;
    } else {
        return -2;
    }
}

int ioctl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd      = static_cast<int>(arg0);
    auto request = static_cast<unsigned long>(arg1);
    long tid     = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%ld, request=%ld)", arg0, arg1);

    int res = capio_ioctl(fd, request, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_IOCTL_HPP
