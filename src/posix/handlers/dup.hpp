#ifndef CAPIO_POSIX_HANDLERS_DUP_HPP
#define CAPIO_POSIX_HANDLERS_DUP_HPP

#include <libsyscall_intercept_hook_point.h>
#include <syscall.h>

#include "globals.hpp"
#include "utils/requests.hpp"

inline int capio_dup(int fd, long tid) {
    CAPIO_DBG("capio_dup TID[%ld] FD[%d]: enter\n", tid, fd);

    auto it = files->find(fd);
    if (it != files->end()) {

        int res = open("/dev/null", O_WRONLY);
        if (res == -1)
            err_exit("open in capio_dup", "capio_dup");
        dup_request(fd, res, tid);
        (*files)[res] = (*files)[fd];
        (*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];

        CAPIO_DBG("capio_dup TID[%ld] FD[%d]: return %d\n", tid, fd, res);

        return res;

    } else {
        CAPIO_DBG("capio_dup TID[%ld] FD[%d]: external file, return -2\n", tid, fd);
        return -2;
    }
}

inline int capio_dup2(int fd, int fd2, long tid) {

    CAPIO_DBG("capio_dup2 TID[%ld] FD[%d] FD2[%d]: enter\n", tid, fd, fd2);

    auto it = files->find(fd);
    if (it != files->end()) {

        int res = syscall_no_intercept(SYS_dup2, fd, fd2);
        if (res == -1) {
            CAPIO_DBG("capio_dup2 TID[%ld] FD[%d] FD2[%d]: call to SYS_dup failed, return -1\n", tid, fd, fd2);
            return -1;
        }
        dup_request(fd, res, tid);
        (*files)[res] = (*files)[fd];
        (*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];

        CAPIO_DBG("capio_dup2 TID[%ld] FD[%d] FD2[%d]: return %d\n", tid, fd, fd2, res);
        return res;
    } else {
        CAPIO_DBG("capio_dup2 TID[%ld] FD[%d] FD2[%d]: external file, return -2\n", tid, fd, fd2);
        return -2;
    }
}


int dup_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    int res = capio_dup(static_cast<int>(arg0), tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int dup2_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    int res = capio_dup2(static_cast<int>(arg0), static_cast<int>(arg1), tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_DUP_HPP
