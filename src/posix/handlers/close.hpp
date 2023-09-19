#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#include "globals.hpp"
#include "utils/requests.hpp"

inline int capio_close(int fd, long tid) {

    START_LOG(tid, "call(fd=%ld)", fd);

    if (files->find(fd) != files->end()) {
        close_request(fd, tid);
        capio_files_descriptors->erase(fd);
        files->erase(fd);

        return 0;
    } else {
        return -2;
    }
}


int close_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result){
    int fd = static_cast<int>(arg0);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d)", fd);

    int res = capio_close(fd, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP
