#ifndef CAPIO_POSIX_HANDLERS_FCNTL_HPP
#define CAPIO_POSIX_HANDLERS_FCNTL_HPP

#if defined(SYS_fcntl)

#include "utils/requests.hpp"

#if defined(SYS_fcntl) || defined(SYS_fcntl64)
inline int fcntl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                         long *result, const pid_t tid) {
    auto fd  = static_cast<int>(arg0);
    auto cmd = static_cast<int>(arg1);
    auto arg = static_cast<int>(arg2);
    START_LOG(tid, "call(fd=%d, cmd=%d, arg=%d)", fd, cmd, arg);

    if (exists_capio_fd(fd)) {
        consent_request_cache_fs->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

#endif // SYS_fcntl
#endif // CAPIO_POSIX_HANDLERS_FCNTL_HPP