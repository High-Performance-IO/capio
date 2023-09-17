#ifndef CAPIO_POSIX_HANDLERS_UNLINK_HPP
#define CAPIO_POSIX_HANDLERS_UNLINK_HPP

#include "globals.hpp"

inline off64_t capio_unlink(const char *pathname,long tid) {

    CAPIO_DBG("capio_unlink TID[%ld] PATHNAME[%s]: enter\n", tid, pathname);

    if (capio_dir == nullptr) {
        //unlink can be called before initialization (see initialize_from_snapshot)

        CAPIO_DBG("capio_unlink TID[%ld] PATHNAME[%s]: invalid CAPIO_DIR, return -2\n", tid, pathname);

        return -2;
    }
    std::string abs_path = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
    if (abs_path.length() == 0) {

        CAPIO_DBG("capio_unlink TID[%ld] PATHNAME[%s]: invalid abs_path, return -2\n", tid, pathname);

        return -2;
    }

    off64_t res = capio_unlink_abs(abs_path, tid);

    CAPIO_DBG("capio_unlink TID[%ld] PATHNAME[%s]: return %ld\n", tid, pathname, res);

    return res;
}

int unlink_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result,  long tid){

    const char *pathname = reinterpret_cast<const char *>(arg0);
    off64_t res = capio_unlink(pathname, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_UNLINK_HPP
