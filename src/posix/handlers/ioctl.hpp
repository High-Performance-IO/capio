#ifndef CAPIO_POSIX_HANDLERS_IOCTL_HPP
#define CAPIO_POSIX_HANDLERS_IOCTL_HPP

#if defined(SYS_ioctl)

int ioctl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd      = static_cast<int>(arg0);
    auto request = static_cast<unsigned long>(arg1);
    auto tid     = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    START_LOG(tid, "call(fd=%d, request=%ld)", fd, request, tid);

    if (exists_capio_fd(fd)) {
        consent_request_cache_fs->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

#endif // SYS_ioctl
#endif // CAPIO_POSIX_HANDLERS_IOCTL_HPP
