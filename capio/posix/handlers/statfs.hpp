#ifndef CAPIO_POSIX_HANDLERS_STATFS_HPP
#define CAPIO_POSIX_HANDLERS_STATFS_HPP

#if defined(SYS_fstatfs)

#include "utils/cache.hpp"

int fstatfs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    auto fd = static_cast<int>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d, buf=0x%08x)", fd, arg1);

    if (!fd_table->contains(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    MEMORY_MODE({
        auto *buf = reinterpret_cast<struct statfs *>(arg1);
        *result = static_cast<int>(syscall_no_intercept(SYS_statfs, get_capio_dir().c_str(), buf));
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    })
    FS_MODE({
        // The kernel runs it, once the file is there
        consent_request_cache->consent_request(fd_table->path(fd), syscall_no_intercept(SYS_gettid),
                                               __FUNCTION__);
        return CAPIO_POSIX_SYSCALL_SKIP;
    })
}

#endif // SYS_fstatfs
#endif // CAPIO_POSIX_HANDLERS_STATFS_HPP
