#ifndef CAPIO_POSIX_HANDLERS_ACCESS_HPP
#define CAPIO_POSIX_HANDLERS_ACCESS_HPP

#include "globals.hpp"
#include "utils/filesystem.hpp"

inline off64_t capio_access(const char *pathname, int mode, long tid) {

    CAPIO_DBG("capio_access TID[%ld] PATHNAME[%s] MODE[%d]: enter");

    std::string abs_pathname = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
    abs_pathname = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
    if (abs_pathname.length() == 0) {
        errno = ENONET;
        CAPIO_DBG("capio_access TID[%ld] PATHNAME[%s] MODE[%d]: return -1");
        return -1;
    }
    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), abs_pathname.begin());
    if (res.first == capio_dir->end()) {
        return access_request(abs_pathname.c_str(), tid);
    } else {
        CAPIO_DBG("capio_access TID[%ld] PATHNAME[%s] MODE[%d]: return -2");
        return -2;
    }
}

int access_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long *result,long tid) {

    const char *pathname = reinterpret_cast<const char *>(arg0);

    off64_t res = capio_access(pathname, static_cast<int>(arg1), tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_ACCESS_HPP
