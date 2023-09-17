#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#include "globals.hpp"
#include "utils/requests.hpp"

inline int capio_close(int fd, long tid) {

    CAPIO_DBG("capio_close TID[%d], FD[%d]: enter\n", tid, fd);

    auto it = files->find(fd);
    if (it != files->end()) {
        close_request(fd, tid);
        capio_files_descriptors->erase(fd);
        files->erase(fd);
        return close(fd);
    } else {
        CAPIO_DBG("capio_close TID[%d], FD[%d]: external file, return -2\n", tid, fd);
        return -2;
    }
}


int close_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long tid){

    int fd = static_cast<int>(arg0);
    int res = capio_close(fd, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP
