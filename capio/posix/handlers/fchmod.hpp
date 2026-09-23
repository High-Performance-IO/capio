#ifndef CAPIO_POSIX_HANDLERS_FCHMOD_HPP
#define CAPIO_POSIX_HANDLERS_FCHMOD_HPP

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

#endif // CAPIO_POSIX_HANDLERS_FCHMOD_HPP
