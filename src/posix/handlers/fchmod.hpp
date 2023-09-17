#ifndef CAPIO_POSIX_HANDLERS_FCHMOD_HPP
#define CAPIO_POSIX_HANDLERS_FCHMOD_HPP

#include "globals.hpp"

int fchmod_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long tid){

    int fd = static_cast<int>(arg0);

    CAPIO_DBG("fchmod_handler TID[%ld] FD[%d]: enter\n", tid, fd);

    if (files->find(fd) == files->end()) {
      CAPIO_DBG("fchmod_handler TID[%ld] FD[%d]: external file, return 1\n", tid, fd);
      return 1;
    }

    CAPIO_DBG("fchmod_handler TID[%ld] FD[%d]: return 0\n", tid, fd);

    *result = -errno;
    return 0;
}
#endif // CAPIO_POSIX_HANDLERS_FCHMOD_HPP
