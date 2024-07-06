#ifndef CAPIO_POSIX_HANDLERS_FCHOWN_HPP
#define CAPIO_POSIX_HANDLERS_FCHOWN_HPP

#if defined(SYS_chown)

int fchown_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd = static_cast<int>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d)", fd);

    if (!exists_capio_fd(fd)) {
        LOG("Syscall refers to file not handled by capio. Skipping it!");
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    // Upon success fchown shall return 0
    // Since capio does not handle permission, we will be
    // Upon the assumption that all the operations occurs with success
    *result = 0;
    LOG("File is present in capio. Ignoring fchmod operation");
    return CAPIO_POSIX_SYSCALL_SUCCESS;
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // SYS_chown
#endif // CAPIO_POSIX_HANDLERS_FCHOWN_HPP
