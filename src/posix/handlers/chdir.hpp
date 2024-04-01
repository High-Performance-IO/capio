#ifndef CAPIO_POSIX_HANDLERS_CHDIR_HPP
#define CAPIO_POSIX_HANDLERS_CHDIR_HPP

#if defined(SYS_chdir)

#include "utils/filesystem.hpp"

/*
 * chdir could be done to a CAPIO dir that is not present in the filesystem.
 * For this reason if chdir is done to a CAPIO directory we don't give control
 * to the kernel.
 */

int chdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(path=%s)", pathname.data());

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
        errno = 0;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }

    // if not a capio path, then control is given to kernel
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_chdir
#endif // CAPIO_POSIX_HANDLERS_CHDIR_HPP
