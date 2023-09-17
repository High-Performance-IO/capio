#ifndef CAPIO_POSIX_HANDLERS_IOCTL_HPP
#define CAPIO_POSIX_HANDLERS_IOCTL_HPP

#include "globals.hpp"

inline int capio_ioctl(int fd, unsigned long request, long tid) {

  CAPIO_DBG("capio_ioctl TID[%ld] FD[%d] REQUEST[%d]: enter\n", tid, fd, request);

    if (files->find(fd) != files->end()) {
        errno = ENOTTY;
        CAPIO_DBG("capio_ioctl TID[%ld] FD[%d] REQUEST[%d]: file not found, return -1\n", tid, fd, request);
        return -1;
    } else {
        CAPIO_DBG("capio_ioctl TID[%ld] FD[%d] REQUEST[%d]: file found, return -2\n", tid, fd, request);
        return -2;
    }
}

int ioctl_handler(long arg0, long arg1, long arg2,  long arg3, long arg4, long arg5, long* result, long tid){

    int res = capio_ioctl(static_cast<int>(arg0), static_cast<unsigned long>(arg1), tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_IOCTL_HPP
