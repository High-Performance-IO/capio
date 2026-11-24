#ifndef CAPIO_POSIX_HANDLERS_FCHOWN_HPP
#define CAPIO_POSIX_HANDLERS_FCHOWN_HPP

#include "utils/cache.hpp"
#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/metadata.hpp"

int fchown_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd   = static_cast<int>(arg0);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d)", fd);

    if (!exists_capio_fd(fd)) {
        LOG("Syscall refers to file not handled by capio. Skipping it!");
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    MEMORY_MODE({
        // Upon success fchown shall return 0
        // Since capio does not handle permission, we will be
        // Upon the assumption that all the operations occurs with success
        *result = 0;
        LOG("File is present in capio. Ignoring fchmod operation");
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    })
    FS_MODE({
        // The kernel runs it, once the file is there
        consent_request_cache->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);
        return CAPIO_POSIX_SYSCALL_SKIP;
    })
}

int chown_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    FS_MODE({
        return CAPIO_POSIX_SYSCALL_SKIP; // not handled by the FS variant yet: the kernel runs it
    })
    long tid = syscall_no_intercept(SYS_gettid);
    return posix_return_value(
        capio_metadata_access("chown", AT_FDCWD, reinterpret_cast<const char *>(arg0), 0, tid),
        result);
}

int fchownat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    FS_MODE({
        return CAPIO_POSIX_SYSCALL_SKIP; // not handled by the FS variant yet: the kernel runs it
    })
    long tid = syscall_no_intercept(SYS_gettid);
    return posix_return_value(capio_metadata_access("fchownat", static_cast<int>(arg0),
                                                    reinterpret_cast<const char *>(arg1),
                                                    static_cast<int>(arg4), tid),
                              result);
}

#endif // CAPIO_POSIX_HANDLERS_FCHOWN_HPP
