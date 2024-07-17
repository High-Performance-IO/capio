#ifndef CAPIO_POSIX_HANDLERS_STATFS_HPP
#define CAPIO_POSIX_HANDLERS_STATFS_HPP

#if defined(SYS_fstatfs)

int fstatfs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    auto fd   = static_cast<int>(arg0);
    auto *buf = reinterpret_cast<struct statfs *>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(fd=%d, buf=0x%08x)", fd, buf);

    if (exists_capio_fd(fd)) {
        consent_to_proceed_request(get_capio_fd_path(fd), tid);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_fstatfs
#endif // CAPIO_POSIX_HANDLERS_STATFS_HPP
