#ifndef CAPIO_POSIX_HANDLERS_GETDENTS_HPP
#define CAPIO_POSIX_HANDLERS_GETDENTS_HPP

#if defined(SYS_getdents) || defined(SYS_getdents64)

// TODO: too similar to capio_read, refactoring needed
inline int getdents_handler_impl(long arg0, long arg1, long arg2, long *result, bool is64bit) {
    auto fd      = static_cast<int>(arg0);
    auto *buffer = reinterpret_cast<struct linux_dirent64 *>(arg1);
    auto count   = static_cast<off64_t>(arg2);
    auto tid     = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    START_LOG(tid, "call(fd=%d, dirp=0x%08x, count=%ld, is64bit=%s)", fd, buffer, count,
              is64bit ? "true" : "false");

    if (exists_capio_fd(fd)) {
        consent_request_cache_fs->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

inline int getdents_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                            long *result) {
    return getdents_handler_impl(arg0, arg1, arg2, result, false);
}

inline int getdents64_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                              long *result) {
    return getdents_handler_impl(arg0, arg1, arg2, result, true);
}

#endif // SYS_getdents || SYS_getdents64
#endif // CAPIO_POSIX_HANDLERS_GETDENTS_HPP
