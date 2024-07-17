#ifndef CAPIO_POSIX_HANDLERS_LSEEK_HPP
#define CAPIO_POSIX_HANDLERS_LSEEK_HPP

#if defined(SYS_lseek)

#include "utils/common.hpp"

int lseek_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd      = static_cast<int>(arg0);
    auto offset = static_cast<off64_t>(arg1);
    int whence  = static_cast<int>(arg2);
    long tid    = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(fd=%d, offset=%ld, whence=%d)", fd, offset, whence);
    if (exists_capio_fd(fd)) {
        (get_capio_fd_path(fd), tid);
    }

    seek_request(get_capio_fd_path(fd), offset, whence, tid);

    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

#endif // SYS_lseek
#endif // CAPIO_POSIX_HANDLERS_LSEEK_HPP
