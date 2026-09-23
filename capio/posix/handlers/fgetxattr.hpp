#ifndef CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
#define CAPIO_POSIX_HANDLERS_FGETXATTR_HPP

#if defined(SYS_fgetxattr)

int fgetxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    std::string name(reinterpret_cast<const char *>(arg1));
    auto fd = static_cast<int>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(name=%s, value=0x%08x, size=%ld)",
              name.c_str(), reinterpret_cast<void *>(arg2), static_cast<size_t>(arg3));

    if (exists_capio_fd(fd)) {

        CAPIO_STORAGE_CALL(
            {
                if (std::equal(name.begin(), name.end(), "system.posix_acl_access")) {
                    errno   = ENODATA;
                    *result = -errno;
                    return CAPIO_POSIX_SYSCALL_SUCCESS;
                }
                ERR_EXIT("fgetxattr with name %s is not yet supported in CAPIO", name.c_str());
            },
            {
                consent_request_cache_fs->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);
            });
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_fgetxattr
#endif // CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
