#ifndef CAPIO_POSIX_HANDLERS_ACCESS_HPP
#define CAPIO_POSIX_HANDLERS_ACCESS_HPP

#include "globals.hpp"
#include "utils/filesystem.hpp"
#include "capio/filesystem.hpp"

inline off64_t capio_access(const std::string *pathname, mode_t mode, long tid) {
    START_LOG(tid, "call(pathname=%s, mode=%o)", pathname->c_str(), mode);

    const std::string *capio_dir = get_capio_dir();
    const std::string *abs_pathname = capio_posix_realpath(tid, pathname, capio_dir, current_dir);
    if (abs_pathname->length() == 0) {
        errno = ENONET;
        return -1;
    }
    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), abs_pathname->begin());
    if (res.first == capio_dir->end()) {
        return access_request(*abs_pathname, tid);
    } else {
        return -2;
    }
}

inline off64_t capio_faccessat(int dirfd, const std::string *pathname, mode_t mode, int flags, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, mode=%o, flags=%X)", dirfd, pathname->c_str(), mode, flags);

    if (!is_absolute(pathname)) {
        if (dirfd == AT_FDCWD) {
            // pathname is interpreted relative to currdir
            return capio_access(pathname, mode, tid);
        } else {
            if (!is_directory(dirfd)) {
                LOG("dirfd does not point to a directory");
                return -2;
            }
            std::string dir_path = get_dir_path(dirfd);
            if (dir_path.length() == 0) {
                return -2;
            }
            std::string path = dir_path + "/" + *pathname;
            const std::string *capio_dir = get_capio_dir();
            auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), path.begin());
            if (it.first == capio_dir->end()) {
                if (capio_dir->size() == path.size()) {
                    ERR_EXIT("ERROR: unlink to the capio_dir");
                }
                return access_request(path, tid);
            } else {
                return -2;
            }
        }
    } else {
        return access_request(*pathname, tid);
    }
}

int access_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string pathname(reinterpret_cast<const char *>(arg0));
    auto mode = static_cast<mode_t>(arg1);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(pathname=%s, mode=%o)", pathname.c_str(), mode);

    off64_t res = capio_access(&pathname, mode, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int faccessat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    auto mode = static_cast<mode_t>(arg2);
    auto flags = static_cast<int>(arg3);
    long tid = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(dirfd=%d, pathname=%s, mode=%o, flags=%X)", dirfd, pathname.c_str(), mode, flags);

    off64_t res = capio_faccessat(dirfd, &pathname, mode, flags, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_ACCESS_HPP
