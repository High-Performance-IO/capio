#ifndef CAPIO_POSIX_HANDLERS_FCHMOD_HPP
#define CAPIO_POSIX_HANDLERS_FCHMOD_HPP

#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/metadata.hpp"

int fchmod_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd = static_cast<int>(arg0);
    const long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d)", fd);

    // TODO: Handle mode provided bt arg1

    if (!exists_capio_fd(fd)) {
        LOG("Syscall refers to file not handled by capio. Skipping it!");
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    CAPIO_STORAGE_CALL(
        {
            // Upon success fchmod shall return 0
            // Since capio does not handle permission, we will be
            // Upon the assumption that all the operations occurs with success
            *result = 0;
            LOG("File is present in capio. Ignoring fchmod operation");
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        },
        {
            consent_request_cache->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);
            return CAPIO_POSIX_SYSCALL_SKIP;
        });
}

int chmod_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    return posix_return_value(
        capio_metadata_access(AT_FDCWD, reinterpret_cast<const char *>(arg0), 0, tid), result);
}

int fchmodat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    // The kernel's fchmodat has no flags argument: fchmodat2 added it.
    return posix_return_value(
        capio_metadata_access(static_cast<int>(arg0), reinterpret_cast<const char *>(arg1), 0, tid),
        result);
}

#endif // CAPIO_POSIX_HANDLERS_FCHMOD_HPP
