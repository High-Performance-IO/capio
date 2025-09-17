#ifndef CAPIO_POSIX_HANDLERS_FCHMOD_HPP
#define CAPIO_POSIX_HANDLERS_FCHMOD_HPP

#if defined(SYS_chmod) || defined(SYS_fchmod)

inline int fchmod_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result,
                   const pid_t tid) {
    const int fd = static_cast<int>(arg0);

    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d)", fd);

    if (!exists_capio_fd(fd)) {
        LOG("Syscall refers to file not handled by capio. Skipping it!");
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    consent_request_cache_fs->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);

    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_chmod
#endif // CAPIO_POSIX_HANDLERS_FCHMOD_HPP