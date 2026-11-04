#ifndef CAPIO_POSIX_HANDLERS_CHDIR_HPP
#define CAPIO_POSIX_HANDLERS_CHDIR_HPP

#if defined(SYS_chdir) || defined(SYS_fchdir)

#include "utils/filesystem.hpp"

/* CAPIO's working directory, after the kernel changed its own. */
inline void follow_kernel_current_dir() {
    char buf[PATH_MAX];
    if (syscall_no_intercept(SYS_getcwd, buf, PATH_MAX) > 0) {
        set_current_dir(buf);
    }
}
#endif

#if defined(SYS_chdir)

/*
 * chdir could be done to a CAPIO dir that is not present in the filesystem.
 * For this reason if chdir is done to a CAPIO directory we don't give control
 * to the kernel.
 */

int chdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    START_LOG(syscall_no_intercept(SYS_gettid), "call(path=%s)", pathname.data());

    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    std::filesystem::path path(pathname);
    if (path.is_relative()) {
        path = capio_posix_realpath(path);
        if (path.empty()) {
            *result = -errno;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
    }

    if (is_capio_path(path)) {
        set_current_dir(path);
        errno   = 0;
        *result = 0;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }

    // Not a CAPIO path: the kernel changes directory, and CAPIO follows it, since it resolves
    // relative paths against its own working directory.
    *result = syscall_no_intercept(SYS_chdir, arg0);
    if (*result == 0) {
        follow_kernel_current_dir();
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // SYS_chdir

#if defined(SYS_fchdir)

int fchdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const int fd = static_cast<int>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d)", fd);

    if (exists_capio_fd(fd)) {
        set_current_dir(get_capio_fd_path(fd));
        *result = 0;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    *result = syscall_no_intercept(SYS_fchdir, fd);
    if (*result == 0) {
        follow_kernel_current_dir();
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // SYS_fchdir
#endif // CAPIO_POSIX_HANDLERS_CHDIR_HPP
