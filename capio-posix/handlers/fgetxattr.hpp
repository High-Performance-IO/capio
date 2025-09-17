#ifndef CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
#define CAPIO_POSIX_HANDLERS_FGETXATTR_HPP

#if defined(SYS_fgetxattr)

inline int fgetxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                             long *result, const pid_t tid) {
    std::string name(reinterpret_cast<const char *>(arg1));
    auto *value = reinterpret_cast<void *>(arg2);
    auto size   = static_cast<size_t>(arg3);
    auto fd     = static_cast<int>(arg0);
    START_LOG(tid, "call(name=%s, value=0x%08x, size=%ld)", name.c_str(), value, size);

    if (exists_capio_fd(fd)) {
        consent_request_cache_fs->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_fgetxattr
#endif // CAPIO_POSIX_HANDLERS_FGETXATTR_HPP