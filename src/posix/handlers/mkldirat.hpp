#ifndef CAPIO_POSIX_HANDLERS_MKLDIRAT_HPP
#define CAPIO_POSIX_HANDLERS_MKLDIRAT_HPP

#include "mkdir.hpp"

inline off64_t capio_mkdirat(int dirfd,const char *pathname,mode_t mode,long tid) {

    CAPIO_DBG("capio_mkdirat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d]: enter\n", tid, dirfd, pathname, mode);

    std::string path_to_check;
    if (is_absolute(pathname)) {
        path_to_check = pathname;

        CAPIO_DBG("capio_mkdirat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d]: absolute path is %s\n", tid, dirfd, pathname, mode, path_to_check.c_str());

    } else {
        if (dirfd == AT_FDCWD) {
            path_to_check = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
            if (path_to_check.length() == 0) {
              CAPIO_DBG("capio_mkdirat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d]: AT_FDCWD invalid path, return -2\n", tid, dirfd, pathname, mode, path_to_check.c_str());
              return -2;
            }

            CAPIO_DBG("capio_mkdirat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d]: AT_FDCWD %s\n", tid, dirfd, pathname, mode, path_to_check.c_str());

        } else {

            if (is_directory(dirfd) != 1) {
              CAPIO_DBG("capio_mkdirat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d]: not a directory, return -2\n", tid, dirfd, pathname, mode, path_to_check.c_str());
              return -2;
            }
            std::string dir_path = get_dir_path(pathname, dirfd);
            if (dir_path.length() == 0) {
              CAPIO_DBG("capio_mkdirat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d]: invalid path, return -2\n", tid, dirfd, pathname, mode, path_to_check.c_str());
              return -2;
            }
            path_to_check = dir_path + "/" + pathname;
            CAPIO_DBG("capio_mkdirat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d]: with dirfd path_to_check %s\n", tid, dirfd, pathname, mode, path_to_check.c_str());
        }
    }

    CAPIO_DBG("capio_mkdirat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d]: call request_mkdir\n", tid, dirfd, pathname, mode);

    off64_t res = request_mkdir(path_to_check, tid);

    CAPIO_DBG("capio_mkdirat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d]: return %ld\n", tid, dirfd, pathname, mode, res);

    return res;
}


int mkdirat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    const char *pathname = reinterpret_cast<const char *>(arg1);
    int dirfd = arg0;
    off64_t res = capio_mkdirat(dirfd,pathname,static_cast<mode_t>(arg2),tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_MKLDIRAT_HPP
