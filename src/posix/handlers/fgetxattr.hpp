#ifndef CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
#define CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
int fgetxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    std::string name(reinterpret_cast<const char *>(arg1));
    auto *value = reinterpret_cast<void *>(arg2);
    auto size   = static_cast<size_t>(arg3);
    long tid    = syscall_no_intercept(SYS_gettid);
    auto fd     = static_cast<int>(arg0);
    START_LOG(tid, "call(name=%s, value=0x%08x, size=%ld)", name.c_str(), value, size);

    if (exists_capio_fd(fd)) {
        if (std::equal(name.begin(), name.end(), "system.posix_acl_access")) {
            errno   = ENODATA;
            *result = -errno;
            return 0;
        } else {
            ERR_EXIT("fgetxattr with name %s is not yet supported in CAPIO", name.c_str());
        }
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
