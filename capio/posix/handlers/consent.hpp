#ifndef CAPIO_POSIX_HANDLERS_CONSENT_HPP
#define CAPIO_POSIX_HANDLERS_CONSENT_HPP

#if !CAPIO_MEM_STREAM

#include "utils/cache.hpp"
#include "utils/filesystem.hpp"

/// @brief FS variant: calls on a descriptor that only wait for the path, like stat, then the kernel
/// runs them
int fd_consent_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                          long *result) {
    const int fd = static_cast<int>(arg0);
    if (exists_capio_fd(fd)) {
        consent_request_cache->consent_request(get_capio_fd_path(fd),
                                               syscall_no_intercept(SYS_gettid), __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // !CAPIO_MEM_STREAM
#endif // CAPIO_POSIX_HANDLERS_CONSENT_HPP
