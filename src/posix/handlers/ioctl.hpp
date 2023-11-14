#ifndef CAPIO_POSIX_HANDLERS_IOCTL_HPP
#define CAPIO_POSIX_HANDLERS_IOCTL_HPP

int ioctl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd      = static_cast<int>(arg0);
    auto request = static_cast<unsigned long>(arg1);
    long tid     = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, request=%ld)", fd, request, tid);

    if (exists_capio_fd(fd)) {
        errno   = ENOTTY;
        *result = -errno;
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_IOCTL_HPP
