#ifndef CAPIO_POSIX_HANDLERS_CHDIR_HPP
#define CAPIO_POSIX_HANDLERS_CHDIR_HPP

#include "globals.hpp"
#include "utils/filesystem.hpp"

/*
 * chdir could be done to a CAPIO dir that is not present in the filesystem.
 * For this reason if chdir is done to a CAPIO directory we don't give control
 * to the kernel.
 */

inline int capio_chdir(const std::string *path, long tid) {
    const std::string *capio_dir     = get_capio_dir();
    const std::string *path_to_check = path;
    START_LOG(tid, "call(path=%s)", path);

    if (!is_absolute(path)) {
        path_to_check = capio_posix_realpath(tid, path, capio_dir, current_dir);
    }

    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check->c_str());

    if (res.first == capio_dir->end()) {
        delete current_dir;
        current_dir = path_to_check;
        return 0;
    } else {
        return -2;
    }
}

int chdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string path(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(path=%s)", path.c_str());

    const std::string *pathname_abs =
        capio_posix_realpath(tid, &path, get_capio_dir(), current_dir);

    int res = capio_chdir(pathname_abs, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_CHDIR_HPP
