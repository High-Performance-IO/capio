#ifndef CAPIO_POSIX_HANDLERS_STATFS_HPP
#define CAPIO_POSIX_HANDLERS_STATFS_HPP

#if defined(SYS_fstatfs) || defined(SYS_fstatfs64)

int fstatfs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    auto fd  = static_cast<int>(arg0);
    auto tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    START_LOG(tid, "call(fd=%d)", fd);

    if (exists_capio_fd(fd)) {
        consent_request_cache_fs->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_fstatfs || SYS_fstatfs64
#endif // CAPIO_POSIX_HANDLERS_STATFS_HPP
