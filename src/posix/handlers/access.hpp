#ifndef CAPIO_POSIX_HANDLERS_ACCESS_HPP
#define CAPIO_POSIX_HANDLERS_ACCESS_HPP

#include "utils/filesystem.hpp"
#include "utils/functions.hpp"

inline off64_t capio_access(const std::string *pathname, mode_t mode, long tid) {
    START_LOG(tid, "call(pathname=%s, mode=%o)", pathname->c_str(), mode);

    const std::string abs_pathname = capio_posix_realpath(pathname);
    if (abs_pathname.empty()) {
        errno = ENONET;
        return POSIX_SYSCALL_ERRNO;
    }
    if (is_capio_path(abs_pathname)) {
        return access_request(abs_pathname, tid);
    } else {
        return POSIX_SYSCALL_REQUEST_SKIP;
    }
}

inline off64_t capio_faccessat(int dirfd, const std::string *pathname, mode_t mode, int flags,
                               long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, mode=%o, flags=%X)", dirfd, pathname->c_str(), mode,
              flags);

    if (!is_absolute(pathname)) {
        if (dirfd == AT_FDCWD) {
            // pathname is interpreted relative to currdir
            return capio_access(pathname, mode, tid);
        } else {
            if (!is_directory(dirfd)) {
                LOG("dirfd does not point to a directory");
                return POSIX_SYSCALL_REQUEST_SKIP;
            }
            std::string dir_path = get_dir_path(dirfd);
            if (dir_path.empty()) {
                return POSIX_SYSCALL_REQUEST_SKIP;
            }
            std::string path = dir_path + "/" + *pathname;
            return is_capio_path(path) ? access_request(path, tid) : -2;
        }
    } else {
        return access_request(*pathname, tid);
    }
}

int access_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string pathname(reinterpret_cast<const char *>(arg0));
    auto mode = static_cast<mode_t>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_access(&pathname, mode, tid), result);
}

int faccessat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    auto dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    auto mode  = static_cast<mode_t>(arg2);
    auto flags = static_cast<int>(arg3);
    long tid   = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_faccessat(dirfd, &pathname, mode, flags, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_ACCESS_HPP
