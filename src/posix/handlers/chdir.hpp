#ifndef CAPIO_POSIX_HANDLERS_CHDIR_HPP
#define CAPIO_POSIX_HANDLERS_CHDIR_HPP

#include "utils/filesystem.hpp"

/*
 * chdir could be done to a CAPIO dir that is not present in the filesystem.
 * For this reason if chdir is done to a CAPIO directory we don't give control
 * to the kernel.
 */

int chdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string path(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(path=%s)", path.c_str());

    if (!is_absolute(&path)) {
        path = capio_posix_realpath(&path);
        if (path.empty()) {
            *result = -errno;
            return POSIX_SYSCALL_SUCCESS;
        }
    }

    if (is_capio_path(path)) {
        set_current_dir(path);
        errno = 0;
        return POSIX_SYSCALL_SUCCESS;
    }

    // if not a capio path, then control is given to kernel
    return POSIX_SYSCALL_SKIP;
}

#endif // CAPIO_POSIX_HANDLERS_CHDIR_HPP
