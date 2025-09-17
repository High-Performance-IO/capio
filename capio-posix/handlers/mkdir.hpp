#ifndef CAPIO_POSIX_HANDLERS_MKDIR_HPP
#define CAPIO_POSIX_HANDLERS_MKDIR_HPP

#include "utils/common.hpp"
#include "utils/filesystem.hpp"

inline off64_t capio_mkdirat(int dirfd, const std::string_view &pathname, mode_t mode, pid_t tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, mode=%o)", dirfd, pathname.data(), mode);

    if (!is_capio_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    std::filesystem::path path(pathname);
    if (path.is_relative()) {
        if (dirfd == AT_FDCWD) {
            path = capio_posix_realpath(path);
            if (path.empty()) {
                return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
            }
        } else {
            if (!is_directory(dirfd)) {
                return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
            }
            const std::filesystem::path dir_path = get_dir_path(dirfd);
            if (dir_path.empty()) {
                return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
            }
            path = (dir_path / path).lexically_normal();
        }
    }

    if (is_capio_path(path)) {
        create_request(-1, path, tid);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

inline off64_t capio_rmdir(const std::string_view &pathname, pid_t tid) {
    START_LOG(tid, "call(pathname=%s)", pathname.data());

    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

#if defined(SYS_mkdir)
inline int mkdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                         long *result, const pid_t tid) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    auto mode = static_cast<mode_t>(arg1);

    return posix_return_value(capio_mkdirat(AT_FDCWD, pathname, mode, tid), result);
}
#endif // SYS_mkdir

#if defined(SYS_mkdirat)
inline int mkdirat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                           long *result, const pid_t tid) {
    int dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    auto mode = static_cast<mode_t>(arg2);

    return posix_return_value(capio_mkdirat(dirfd, pathname, mode, tid), result);
}
#endif // SYS_mkdirat

#if defined(SYS_rmdir)
inline int rmdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                         long *result, const pid_t tid) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));

    return posix_return_value(capio_rmdir(pathname, tid), result);
}
#endif // SYS_rmdir

#endif // CAPIO_POSIX_HANDLERS_MKDIR_HPP