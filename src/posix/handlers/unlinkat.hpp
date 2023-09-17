#ifndef CAPIO_POSIX_HANDLERS_UNLINKAT_HPP
#define CAPIO_POSIX_HANDLERS_UNLINKAT_HPP

#include "globals.hpp"
#include "unlink.hpp"
#include "utils/filesystem.hpp"

inline off64_t capio_unlinkat(int dirfd,const char *pathname,int flags,long tid) {

    CAPIO_DBG("capio_unlinkat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d]: enter\n", tid, dirfd, pathname, flags);

    if (capio_dir->length() == 0) {
        CAPIO_DBG("capio_unlinkat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d]: invalid CAPIO_DIR, return -2\n",  tid, dirfd, pathname, flags);
        return -2;
    }
    off64_t res;
    if (!is_absolute(pathname)) {
        if (dirfd == AT_FDCWD) {
            //pathname is interpreted relative to currdir
            res = capio_unlink(pathname, tid);
        } else {
            if (is_directory(dirfd) != 1) {
              CAPIO_DBG("capio_unlinkat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d]: not a directory, return -2\n",  tid, dirfd, pathname, flags);
              return -2;
            }
            std::string dir_path = get_dir_path(pathname, dirfd);
            if (dir_path.length() == 0) {
              CAPIO_DBG("capio_unlinkat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d]: invalid path, return -2\n",  tid, dirfd, pathname, flags);
              return -2;
            }
            std::string path = dir_path + "/" + pathname;
            CAPIO_DBG("capio_unlinkat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d]: unlink path %s\n",  tid, dirfd, pathname, flags, path.c_str());

            res = capio_unlink_abs(path, tid);
        }
    } else {
        CAPIO_DBG("capio_unlinkat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d]: absolute path, ignore dirfd\n",  tid, dirfd, pathname, flags);
        res = capio_unlink_abs(pathname, tid);
    }

    CAPIO_DBG("capio_unlinkat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d]: return %ld\n",  tid, dirfd, pathname, flags, res);

    return res;
}

int unlinkat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    const char *pathname = reinterpret_cast<const char *>(arg1);
    off64_t res = capio_unlinkat(static_cast<int>(arg0), pathname, static_cast<int>(arg2), tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_UNLINKAT_HPP
