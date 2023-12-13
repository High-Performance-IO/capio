#ifndef CAPIO_POSIX_HANDLERS_FCHMOD_HPP
#define CAPIO_POSIX_HANDLERS_FCHMOD_HPP

int fchmod_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd = static_cast<int>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d)", fd);

    if (!exists_capio_fd(fd)) {
        return POSIX_SYSCALL_SUCCESS;
    }
    *result = -errno;

    return POSIX_SYSCALL_SUCCESS;
}

#endif // CAPIO_POSIX_HANDLERS_FCHMOD_HPP
