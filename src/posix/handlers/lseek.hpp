#ifndef CAPIO_POSIX_HANDLERS_LSEEK_HPP
#define CAPIO_POSIX_HANDLERS_LSEEK_HPP

#if defined(SYS_lseek)

#include "utils/common.hpp"

int lseek_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd      = static_cast<int>(arg0);
    auto offset = static_cast<capio_off64_t>(arg1);
    int whence  = static_cast<int>(arg2);
    auto tid    = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    START_LOG(tid, "call(fd=%d, offset=%ld, whence=%d)", fd, offset, whence);
    if (exists_capio_fd(fd)) {
        capio_off64_t computed_offset = 0;

        if (whence == SEEK_CUR) {
            computed_offset = get_capio_fd_offset(fd) + offset;
        } else {
            computed_offset = offset;
        }

        // computed_offset = seek_request(get_capio_fd_path(fd), computed_offset, whence, tid, fd);

        set_capio_fd_offset(fd, computed_offset);
    }

    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

#endif // SYS_lseek
#endif // CAPIO_POSIX_HANDLERS_LSEEK_HPP
