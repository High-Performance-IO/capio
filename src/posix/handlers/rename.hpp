#ifndef CAPIO_POSIX_HANDLERS_RENAME_HPP
#define CAPIO_POSIX_HANDLERS_RENAME_HPP

#include <filesystem>

#include "globals.hpp"
#include "utils/filesystem.hpp"

inline off64_t capio_rename(const std::string &oldpath, const std::string &newpath, long tid) {
    START_LOG(tid, "call(oldpath=%s, newpath=%s)", oldpath.c_str(), newpath.c_str());

    const std::string *capio_dir = get_capio_dir();
    std::string oldpath_abs, newpath_abs;

    if (is_absolute(&oldpath)) {
        oldpath_abs = oldpath;
    } else {
        oldpath_abs = *capio_posix_realpath(tid, &oldpath, capio_dir, current_dir);
    }

    bool oldpath_capio = is_capio_path(oldpath_abs);

    if (is_absolute(&newpath)) { // TODO: move this control inside create_absolute_path
        newpath_abs = newpath;
    } else {
        newpath_abs = *capio_posix_realpath(tid, &newpath, capio_dir, current_dir);
    }

    bool newpath_capio = is_capio_path(newpath_abs);

    if (is_prefix(oldpath_abs, newpath_abs)) { // TODO: The check is more complex
        errno = EINVAL;
        return -1;
    }

    if (oldpath_capio) {
        capio_files_paths->erase(oldpath_abs);
        capio_files_paths->insert(newpath_abs);
        return rename_request(tid, oldpath_abs, newpath_abs);
    } else {
        if (newpath_capio) {
            std::filesystem::copy(oldpath_abs, newpath_abs);
            return -1;
        } else { // Both aren't CAPIO paths
            return -2;
        }
    }
}

int rename_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::string oldpath(reinterpret_cast<const char *>(arg0));
    const std::string newpath(reinterpret_cast<const char *>(arg1));
    long tid = syscall_no_intercept(SYS_gettid);

    off64_t res = capio_rename(oldpath, newpath, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_RENAME_HPP
