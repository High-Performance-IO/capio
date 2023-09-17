#ifndef CAPIO_POSIX_HANDLERS_CHDIR_HPP
#define CAPIO_POSIX_HANDLERS_CHDIR_HPP

#include "globals.hpp"
#include "utils/filesystem.hpp"

/*
 * chdir could be done to a CAPIO dir that is not present in the filesystem.
 * For this reason if chdir is done to a CAPIO directory we don't give control
 * to the kernel.
 */

inline int capio_chdir(const char *path, long tid) {

    std::string path_to_check;
    CAPIO_DBG("capio_chdir TID[%ld] PATH[%s]: enter\n", tid, path);

    if (is_absolute(path)) {
        path_to_check = path;
        CAPIO_DBG("capio_chdir TID[%ld] PATH[%s]: absolute path %s\n", tid, path, path_to_check.c_str());
    } else {
        path_to_check = create_absolute_path(path, capio_dir, current_dir, stat_enabled);
    }

    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());

    CAPIO_DBG("capio_chdir TID[%ld] PATH[%s]: CAPIO_DIR is set to %s\n", tid, path, capio_dir->c_str());

    if (res.first == capio_dir->end()) {
        delete current_dir;


        CAPIO_DBG("capio_chdir TID[%ld] PATH[%s]: current dir changed to %s\n", tid, path_to_check.c_str());

        current_dir = new std::string(path_to_check);
        return 0;
    } else {
        return -2;
    }
}

int chdir_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long *result,long tid) {

    const char *path = reinterpret_cast<const char *>(arg0);
    int res = capio_chdir(path, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}


#endif // CAPIO_POSIX_HANDLERS_CHDIR_HPP
