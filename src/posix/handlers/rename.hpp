#ifndef CAPIO_POSIX_HANDLERS_RENAME_HPP
#define CAPIO_POSIX_HANDLERS_RENAME_HPP

#include "globals.hpp"
#include "utils/filesystem.hpp"

inline int capio_rename(const char *oldpath,const char *newpath,long tid) {
    std::string oldpath_abs, newpath_abs;

    CAPIO_DBG("capio_rename TID[%ld] OLDPATH[%s] NEWPATH[%s]: enter\n", oldpath, newpath, tid);

    if (is_absolute(oldpath)) {
        oldpath_abs = oldpath;
    } else {
        oldpath_abs = create_absolute_path(oldpath, capio_dir, current_dir, stat_enabled);
    }

    bool oldpath_capio = is_capio_path(oldpath_abs, capio_dir);

    if (is_absolute(newpath)) { //TODO: move this control inside create_absolute_path
        newpath_abs = newpath;
    } else {
        newpath_abs = create_absolute_path(newpath, capio_dir, current_dir, stat_enabled);
    }

    bool newpath_capio = is_capio_path(newpath_abs, capio_dir);

    if (is_prefix(oldpath_abs, newpath_abs)) {//TODO: The check is more complex
        errno = EINVAL;
        CAPIO_DBG("capio_rename TID[%ld] OLDPATH[%s] NEWPATH[%s]: return -1\n", oldpath, newpath, tid);
        return -1;
    }

    if (oldpath_capio) {
#ifdef CAPIOLOG
        if (newpath_capio) {
            CAPIO_DBG("capio_rename TID[%ld] OLDPATH[%s] NEWPATH[%s]: rename src %s -> %s\n", oldpath, newpath, tid, oldpath_abs.c_str(), newpath_abs.c_str());
        } else {
            CAPIO_DBG("capio_rename TID[%ld] OLDPATH[%s] NEWPATH[%s]: copy outside CAPIO %s -> %s\n", oldpath, newpath, tid, oldpath_abs.c_str(), newpath_abs.c_str());
        }
#endif
        copy_file(oldpath_abs, newpath_abs);
        unlink_request(oldpath_abs.c_str(), tid);
    } else {
        if (newpath_capio) {
            CAPIO_DBG("capio_rename TID[%ld] OLDPATH[%s] NEWPATH[%s]: copy inside CAPIO %s -> %s\n", oldpath, newpath, tid, oldpath_abs.c_str(), newpath_abs.c_str());
            copy_file(oldpath_abs, newpath_abs);
        } else { //Both aren't CAPIO paths
            CAPIO_DBG("capio_rename TID[%ld] OLDPATH[%s] NEWPATH[%s]: return -2, external renaming %s -> %s\n", oldpath, newpath, tid, oldpath_abs.c_str(), newpath_abs.c_str());
            return -2;
        }
    }

    return 0;
}

int rename_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    const char *oldpath = reinterpret_cast<const char *>(arg0);
    const char *newpath = reinterpret_cast<const char *>(arg1);
    int res = capio_rename(oldpath, newpath, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_RENAME_HPP
