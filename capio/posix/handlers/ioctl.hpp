#ifndef CAPIO_POSIX_HANDLERS_IOCTL_HPP
#define CAPIO_POSIX_HANDLERS_IOCTL_HPP

#if defined(SYS_ioctl)

int ioctl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const auto fd = static_cast<int>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d, request=%ld)", fd, arg1);

    if (exists_capio_fd(fd)) {
        errno   = ENOTTY;
        *result = -errno;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

#endif // SYS_ioctl
#endif // CAPIO_POSIX_HANDLERS_IOCTL_HPP
