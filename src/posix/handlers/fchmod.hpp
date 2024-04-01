#ifndef CAPIO_POSIX_HANDLERS_FCHMOD_HPP
#define CAPIO_POSIX_HANDLERS_FCHMOD_HPP

#if defined(SYS_chmod)

int fchmod_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd = static_cast<int>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d)", fd);

    if (!exists_capio_fd(fd)) {
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    *result = -errno;

    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // SYS_chmod
#endif // CAPIO_POSIX_HANDLERS_FCHMOD_HPP
