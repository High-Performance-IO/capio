#ifndef CAPIO_POSIX_HANDLERS_FCNTL_HPP
#define CAPIO_POSIX_HANDLERS_FCNTL_HPP

#if defined(SYS_fcntl)

#include "utils/requests.hpp"

int fcntl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd  = static_cast<int>(arg0);
    auto cmd = static_cast<int>(arg1);
    auto arg = static_cast<int>(arg2);
    auto tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    START_LOG(tid, "call(fd=%d, cmd=%d, arg=%d)", fd, cmd, arg);

    if (exists_capio_fd(fd)) {
        consent_request_cache_fs->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_fcntl
#endif // CAPIO_POSIX_HANDLERS_FCNTL_HPP
