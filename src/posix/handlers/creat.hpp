#ifndef CAPIO_POSIX_HANDLERS_CREAT_HPP
#define CAPIO_POSIX_HANDLERS_CREAT_HPP

#include <cerrno>

#include <fcntl.h>

#include "openat.hpp"
#include "utils/logger.hpp"

int creat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    const char *pathname = reinterpret_cast<const char *>(arg0);

    CAPIO_DBG("creat_handler TID[%d] PATHNAME[%s]: forward to capio_openat\n", tid, pathname);

    int res = capio_openat(AT_FDCWD,pathname,O_CREAT | O_WRONLY | O_TRUNC,true,tid);

    CAPIO_DBG("creat_handler TID[%d] PATHNAME[%s]: capio_openat returned %s\n", tid, pathname, res);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_CREAT_HPP
