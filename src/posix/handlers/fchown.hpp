#ifndef CAPIO_POSIX_HANDLERS_FCHOWN_HPP
#define CAPIO_POSIX_HANDLERS_FCHOWN_HPP

#include "globals.hpp"

/*
int capio_fchown(int fd, uid_t owner, gid_t group ) {
    if (files->find(fd) == files->end())
        return -2; //==true
    else
        return 0;
}

int fchown_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long tid){

    int res = capio_fchown(static_cast<int>(arg0), static_cast<uid_t>(arg1),static_cast<gid_t>(arg2));
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}
*/
//TODO: new fchown. test if it is correct
int fchown_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long tid) {

    int fd = static_cast<int>(arg0);

    CAPIO_DBG("fchown_handler TID[%ld] FD[%d]: enter\n", tid, fd);

    if (files->find(fd) == files->end()) {
      CAPIO_DBG("fchown_handler TID[%ld] FD[%d]: external file, return 1\n", tid, fd);
      return 1;
    }

    CAPIO_DBG("fchown_handler TID[%ld] FD[%d]: return 0\n", tid, fd);

    *result = -errno;
    return 0;
}
#endif // CAPIO_POSIX_HANDLERS_FCHOWN_HPP
