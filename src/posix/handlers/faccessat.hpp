#ifndef CAPIO_POSIX_HANDLERS_FACCESSAT_HPP
#define CAPIO_POSIX_HANDLERS_FACCESSAT_HPP

#include "capio/filesystem.hpp"

#include "access.hpp"
#include "globals.hpp"
#include "utils/filesystem.hpp"

inline off64_t capio_faccessat(int dirfd, const char *pathname, int mode, int flags, long tid) {

    CAPIO_DBG("capio_faccessat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d] FLAGS[%d]: enter\n", tid, dirfd, pathname, mode, flags);

    if (!is_absolute(pathname)) {
        if (dirfd == AT_FDCWD) {
            // pathname is interpreted relative to currdir
            CAPIO_DBG("capio_faccessat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d] FLAGS[%d]: AT_FDCWD, delegate to capio_access\n", tid, dirfd, pathname, mode, flags);
            return capio_access(pathname, mode, tid);
        } else {
            if (is_directory(dirfd) != 1)
                return -2;
            std::string dir_path = get_dir_path(pathname, dirfd);
            if (dir_path.length() == 0)
                return -2;
            std::string path = dir_path + "/" + pathname;
            auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), path.begin());
            if (it.first == capio_dir->end()) {
                if (capio_dir->size() == path.size()) {
                    std::cerr << "ERROR: unlink to the capio_dir " << path << std::endl;
                    exit(1);
                }

                CAPIO_DBG("capio_faccessat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d] FLAGS[%d]: add access_request\n", tid, dirfd, pathname, mode, flags);

                off64_t res = access_request(path.c_str(), tid);

                CAPIO_DBG("capio_faccessat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d] FLAGS[%d]: access_request added, return %ld\n", tid, dirfd, pathname, mode, flags, res);

                return res;

            } else {
                CAPIO_DBG("capio_faccessat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d] FLAGS[%d]: file not found, return -2\n",
                          tid, dirfd, pathname, mode, flags);
                return -2;
            }
        }
    } else {
        CAPIO_DBG("capio_faccessat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d] FLAGS[%d]: absolute path, dirfd is ignored\n", tid, dirfd, pathname, mode, flags);
        CAPIO_DBG("capio_faccessat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d] FLAGS[%d]: add access_request\n", tid, dirfd, pathname, mode, flags);

        off64_t res = access_request(pathname, tid);

        CAPIO_DBG("capio_faccessat TID[%ld] DIRFD[%d] PATHNAME[%s] MODE[%d] FLAGS[%d]: access_request added, return %ld\n", tid, dirfd, pathname, mode, flags, res);

        return res;
    }
}

int faccessat_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long tid){

    const char *pathname = reinterpret_cast<const char *>(arg1);

    off64_t res = capio_faccessat(static_cast<int>(arg0), pathname, static_cast<int>(arg2),static_cast<int>(arg3), tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
       return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_FACCESSAT_HPP
