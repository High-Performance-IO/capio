#ifndef CAPIO_POSIX_HANDLERS_ACCESS_HPP
#define CAPIO_POSIX_HANDLERS_ACCESS_HPP

#include "utils/common.hpp"
#include "utils/filesystem.hpp"

inline off64_t capio_faccessat(int dirfd, const std::string_view &pathname, mode_t mode, int flags,
                               long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, mode=%o, flags=%X)", dirfd, pathname.data(), mode,
              flags);

    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    std::filesystem::path path(pathname);
    if (path.is_relative()) {
        if (dirfd == AT_FDCWD) {
            path = capio_posix_realpath(pathname);
            if (path.empty()) {
                errno = ENONET;
                return CAPIO_POSIX_SYSCALL_ERRNO;
            }
        } else {
            if (!is_directory(dirfd)) {
                LOG("dirfd does not point to a directory");
                return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
            }
            const std::filesystem::path dir_path = get_dir_path(dirfd);
            if (dir_path.empty()) {
                return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
            }
            path = (dir_path / path).lexically_normal();
            return is_capio_path(path) ? access_request(path, tid) : -2;
        }
    }

    if (is_capio_path(path)) {
        return access_request(path, tid);
    } else {
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }
}

int access_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    auto mode = static_cast<mode_t>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_faccessat(AT_FDCWD, pathname, mode, 0, tid), result);
}

int faccessat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    auto dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    auto mode  = static_cast<mode_t>(arg2);
    auto flags = static_cast<int>(arg3);
    long tid   = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_faccessat(dirfd, pathname, mode, flags, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_ACCESS_HPP
