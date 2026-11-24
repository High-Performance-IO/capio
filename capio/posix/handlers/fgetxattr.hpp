#ifndef CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
#define CAPIO_POSIX_HANDLERS_FGETXATTR_HPP

/**
 * The extended attributes of files in memory are not implemented: getxattr, lgetxattr and
 * flistxattr fail with ENOTSUP. In the FS variant the files are real, and the kernel answers
 * without waiting for a commit, as an attribute does not depend on the data.
 */
inline int capio_xattr(long *result) {
    MEMORY_MODE({
        *result = -ENOTSUP;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    })
    FS_MODE({ return CAPIO_POSIX_SYSCALL_SKIP; })
}

int getxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    return capio_xattr(result);
}

int lgetxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    return capio_xattr(result);
}

int flistxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                       long *result) {
    return capio_xattr(result);
}

#if defined(SYS_fgetxattr)

int fgetxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    FS_MODE({ return CAPIO_POSIX_SYSCALL_SKIP; }) // a real file: the kernel answers
    MEMORY_MODE({
        std::string name(reinterpret_cast<const char *>(arg1));
        auto fd = static_cast<int>(arg0);
        START_LOG(syscall_no_intercept(SYS_gettid), "call(name=%s, value=0x%08x, size=%ld)",
                  name.c_str(), reinterpret_cast<void *>(arg2), static_cast<size_t>(arg3));

        if (fd_table->contains(fd)) {
            if (std::equal(name.begin(), name.end(), "system.posix_acl_access")) {
                errno   = ENODATA;
                *result = -errno;
                return CAPIO_POSIX_SYSCALL_SUCCESS;
            } else {
                ERR_EXIT("fgetxattr with name %s is not yet supported in CAPIO", name.c_str());
            }
        }
        return CAPIO_POSIX_SYSCALL_SKIP;
    })
}

#endif // SYS_fgetxattr
#endif // CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
