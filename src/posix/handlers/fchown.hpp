#ifndef CAPIO_POSIX_HANDLERS_FCHOWN_HPP
#define CAPIO_POSIX_HANDLERS_FCHOWN_HPP

int fchown_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd = static_cast<int>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d)", fd);

    if (!exists_capio_fd(fd)) {
        return POSIX_SYSCALL_TO_HANDLE_BY_KERNEL;
    }
    *result = -errno;

    return POSIX_SYSCALL_HANDLED_BY_CAPIO;
}

#endif // CAPIO_POSIX_HANDLERS_FCHOWN_HPP
