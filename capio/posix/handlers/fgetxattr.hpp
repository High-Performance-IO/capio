#ifndef CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
#define CAPIO_POSIX_HANDLERS_FGETXATTR_HPP

#include "utils/common.hpp"
#include "utils/filesystem.hpp"

/*
 * Extended attributes. In the FS variant the files are real, and the kernel answers without
 * waiting for a commit, as an attribute does not depend on the data. The memory variant does not
 * keep attributes: a CAPIO file has none (ENOTSUP, which tools such as ls and cp take as "no
 * attributes"), and any other file goes to the kernel.
 */

/// @brief Memory variant: answer for the CAPIO file @p path, or give any other path to the kernel
inline int capio_xattr_of_path(const char *path, long *result) {
    MEMORY_MODE({
        if (is_capio_path(capio_posix_realpath(path))) {
            *result = -ENOTSUP;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
    })
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#if defined(SYS_getxattr)
int getxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    // getxattr(path, name, value, size)
    const auto *path = reinterpret_cast<const char *>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(path=%s)", path);

    return capio_xattr_of_path(path, result);
}
#endif

#if defined(SYS_lgetxattr)
int lgetxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    // lgetxattr(path, name, value, size). CAPIO paths have no symlinks, so as getxattr
    const auto *path = reinterpret_cast<const char *>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(path=%s)", path);

    return capio_xattr_of_path(path, result);
}
#endif

#if defined(SYS_flistxattr)
int flistxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                       long *result) {
    // flistxattr(fd, list, size)
    [[maybe_unused]] const auto fd = static_cast<int>(arg0); // memory variant, and the log
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d)", fd);

    MEMORY_MODE({
        if (fd_table->contains(fd)) {
            *result = -ENOTSUP;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
    })
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

#if defined(SYS_fgetxattr)
int fgetxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    // fgetxattr(fd, name, value, size). The memory variant uses them, and the log.
    [[maybe_unused]] const auto fd   = static_cast<int>(arg0);
    [[maybe_unused]] const auto name = std::string_view(reinterpret_cast<const char *>(arg1));
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d, name=%s)", fd, name.data());

    MEMORY_MODE({
        if (fd_table->contains(fd)) {
            // Asked by tools such as ls and cp: a CAPIO file has no access control list
            if (name == "system.posix_acl_access") {
                *result = -ENODATA;
                return CAPIO_POSIX_SYSCALL_SUCCESS;
            }
            throw CapioUnimplementedException("fgetxattr of " + std::string(name));
        }
    })
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

#endif // CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
