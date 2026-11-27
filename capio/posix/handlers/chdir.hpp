#ifndef CAPIO_POSIX_HANDLERS_CHDIR_HPP
#define CAPIO_POSIX_HANDLERS_CHDIR_HPP

#if defined(SYS_chdir) || defined(SYS_fchdir)

#include "utils/filesystem.hpp"
#include "utils/tid.hpp"

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
    START_LOG(capio_tid(), "call(path=%s)", pathname.data());

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
        MEMORY_MODE({
            const long tid    = capio_tid();
            const off64_t res = access_request(path, tid);
            if (res == CAPIO_POSIX_SYSCALL_REQUEST_SKIP) {
                // Excluded from CAPIO: the kernel's. By the resolved path, as the kernel stays in
                // the directory before a chdir into CAPIO_DIR
                *result = syscall_no_intercept(SYS_chdir, path.c_str());
                if (*result == 0) {
                    follow_kernel_current_dir();
                }
                return CAPIO_POSIX_SYSCALL_SUCCESS;
            }
            if (res < 0) {
                *result = -ENOENT;
            } else if (stat_request(path, tid).second != 1) {
                *result = -ENOTDIR;
            } else {
                set_current_dir(path);
                *result = 0;
            }
        })
        FS_MODE({
            // A real directory: wait for it, then the kernel changes directory as for any other
            // path
            consent_request_cache->consent_request(path, capio_tid(), __FUNCTION__);
            *result = syscall_no_intercept(SYS_chdir, arg0);
            if (*result == 0) {
                set_current_dir(path);
            }
        })
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
    START_LOG(capio_tid(), "call(fd=%d)", fd);

    MEMORY_MODE({
        if (fd_table->contains(fd)) {
            set_current_dir(fd_table->path(fd));
            *result = 0;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
    })
    // FS variant: a CAPIO directory is a real one too
    *result = syscall_no_intercept(SYS_fchdir, fd);
    if (*result == 0) {
        follow_kernel_current_dir();
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // SYS_fchdir
#endif // CAPIO_POSIX_HANDLERS_CHDIR_HPP
