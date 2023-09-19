#ifndef CAPIO_POSIX_HANDLERS_FCHMOD_HPP
#define CAPIO_POSIX_HANDLERS_FCHMOD_HPP

#include "globals.hpp"

int fchmod_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result){
    long tid = syscall_no_intercept(SYS_gettid);
    int fd = static_cast<int>(arg0);
    START_LOG(tid, "call(fd=%d)", fd);

    if (files->find(fd) == files->end()) {
      return 1;
    }

    *result = -errno;

    return 0;
}
#endif // CAPIO_POSIX_HANDLERS_FCHMOD_HPP
