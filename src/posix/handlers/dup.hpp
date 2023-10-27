#ifndef CAPIO_POSIX_HANDLERS_DUP_HPP
#define CAPIO_POSIX_HANDLERS_DUP_HPP

#include <libsyscall_intercept_hook_point.h>
#include <syscall.h>

#include "globals.hpp"
#include "utils/requests.hpp"

inline int capio_dup(int fd, long tid) {
    START_LOG(tid, "call(fd=%d)", fd);

    auto it = files->find(fd);
    if (it != files->end()) {
        int res = open("/dev/null", O_WRONLY);
        if (res == -1) {
            ERR_EXIT("open in capio_dup");
        }
        dup_request(fd, res, tid);
        (*files)[res] = (*files)[fd];
        (*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];
        return res;
    } else {
        return -2;
    }
}

inline int capio_dup2(int fd, int fd2, long tid) {
    START_LOG(tid, "call(fd=%d, fd2=%d)", fd, fd2);

    auto it = files->find(fd);
    if (it != files->end()) {
        int res = static_cast<int>(syscall_no_intercept(SYS_dup2, fd, fd2));
        if (res == -1) {
            return -1;
        }
        dup_request(fd, res, tid);
        (*files)[res] = (*files)[fd];
        (*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];
        return res;
    } else {
        return -2;
    }
}

int dup_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    int fd = static_cast<int>(arg0);
    START_LOG(tid, "call(%d)", fd);

    int res = capio_dup(fd, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int dup2_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    int fd = static_cast<int>(arg0);
    int fd2 = static_cast<int>(arg1);
    START_LOG(tid, "call(fd=%d, fd2=%d)", fd, fd2);

    int res = capio_dup2(fd, fd2, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_DUP_HPP
