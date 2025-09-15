#ifndef CAPIO_POSIX_HANDLERS_FCHMOD_HPP
#define CAPIO_POSIX_HANDLERS_FCHMOD_HPP

#if defined(SYS_chmod) || defined(SYS_fchmod)

int fchmod_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd   = static_cast<int>(arg0);
    auto tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d)", fd);

    if (!exists_capio_fd(fd)) {
        LOG("Syscall refers to file not handled by capio. Skipping it!");
        return posix_return_value(CAPIO_POSIX_SYSCALL_REQUEST_SKIP, result);
    }

    consent_request_cache_fs->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);

    return posix_return_value(CAPIO_POSIX_SYSCALL_REQUEST_SKIP, result);
}

#endif // SYS_chmod
#endif // CAPIO_POSIX_HANDLERS_FCHMOD_HPP