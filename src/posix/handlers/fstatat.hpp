#ifndef CAPIO_POSIX_HANDLERS_FSTATAT_HPP
#define CAPIO_POSIX_HANDLERS_FSTATAT_HPP

#include "fstat.hpp"
#include "globals.hpp"
#include "lstat.hpp"

inline int capio_fstatat(int dirfd,const char *pathname,struct stat *statbuf,int flags,long tid) {

    CAPIO_DBG("capio_fstatat TID[%ld] DIRFD[%d], PATHNAME[%s] FLAGS[%d]: enter\n", tid, dirfd, pathname, flags);

    if ((flags & AT_EMPTY_PATH) == AT_EMPTY_PATH) {
        if (dirfd == AT_FDCWD) { // operate on currdir
            char *curr_dir = get_current_dir_name();
            std::string path(curr_dir);
            free(curr_dir);
            CAPIO_DBG("capio_fstatat TID[%ld] DIRFD[%d], PATHNAME[%s] FLAGS[%d]: AT_FDCWD, delegate to capio_lstat\n", tid, dirfd, pathname, flags);
            return capio_lstat(path, statbuf, tid);
        } else { // operate on dirfd. in this case dirfd can refer to any type of file
            if (strlen(pathname) == 0) {
              CAPIO_DBG("capio_fstatat TID[%ld] DIRFD[%d], PATHNAME[%s] FLAGS[%d]: empty pathname, delegate to capio_fstat\n", tid, dirfd, pathname, flags);
              return capio_fstat(dirfd, statbuf, tid);
            } else {
                //TODO: set errno
                CAPIO_DBG("capio_fstatat TID[%ld] DIRFD[%d], PATHNAME[%s] FLAGS[%d]: valid pathname, return -1\n", tid, dirfd, pathname, flags);
                return -1;
            }
        }
    }

    if (!is_absolute(pathname)) {
        if (dirfd == AT_FDCWD) {
            // pathname is interpreted relative to currdir
            CAPIO_DBG("capio_fstatat TID[%ld] DIRFD[%d], PATHNAME[%s] FLAGS[%d]: AT_FDCWD, delegate to capio_lstat_wrapper\n", tid, dirfd, pathname, flags);
            return capio_lstat_wrapper(pathname, statbuf, tid);
        } else {
            if (is_directory(dirfd) != 1) {
                CAPIO_DBG("capio_fstatat TID[%ld] DIRFD[%d], PATHNAME[%s] FLAGS[%d]: not a directory, return -2\n", tid, dirfd, pathname, flags);
                return -2;
            }
            auto it = capio_files_descriptors->find(dirfd);
            std::string dir_path;
            if (it == capio_files_descriptors->end())
                dir_path = get_dir_path(pathname, dirfd);
            else
                dir_path = it->second;
            if (dir_path.length() == 0)
                return -2;
            std::string pathstr = pathname;
            if (pathstr.substr(0, 2) == "./") {
                pathstr = pathstr.substr(2, pathstr.length() - 1);
                CAPIO_DBG("capio_fstatat TID[%ld] DIRFD[%d], PATHNAME[%s] FLAGS[%d]: path modified %s\n", tid, dirfd, pathname, flags, pathstr.c_str());
            }
            std::string path;
            if (pathstr[pathstr.length() - 1] == '.')
                path = dir_path;
            else
                path = dir_path + "/" + pathstr;

            CAPIO_DBG("capio_fstatat TID[%ld] DIRFD[%d], PATHNAME[%s] FLAGS[%d]: delegate to capio_lstat\n", tid, dirfd, pathname, flags);

            return capio_lstat(path, statbuf, tid);
        }
    } else {
        CAPIO_DBG("capio_fstatat TID[%ld] DIRFD[%d], PATHNAME[%s] FLAGS[%d]: absolute path, dirfd is ignored", tid, dirfd, pathname, flags);
        CAPIO_DBG("capio_fstatat TID[%ld] DIRFD[%d], PATHNAME[%s] FLAGS[%d]: delegate to capio_lstat\n", tid, dirfd, pathname, flags);
        return capio_lstat(std::string(pathname), statbuf, tid);
    }
}


int fstatat_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long tid){

    const char *pathname = reinterpret_cast<const char *>(arg1);
    auto *statbuf = reinterpret_cast<struct stat *>(arg2);

    int res = capio_fstatat(static_cast<int>(arg0),pathname,statbuf,static_cast<int>(arg3),tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_FSTATAT_HPP
