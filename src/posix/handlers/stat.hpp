#ifndef CAPIO_POSIX_HANDLERS_STAT_HPP
#define CAPIO_POSIX_HANDLERS_STAT_HPP

#include <sys/vfs.h>

#include "capio/env.hpp"

#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"

inline int capio_fstat(int fd, struct stat *statbuf, pid_t tid) {
    START_LOG(tid, "call(fd=%d, statbuf=0x%08x)", fd, statbuf);

    if (exists_capio_fd(fd)) {
        consent_request_cache_fs->consent_request(get_capio_fd_path(fd), tid, __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

inline int capio_lstat(const std::string_view &pathname, struct stat *statbuf, pid_t tid) {
    START_LOG(tid, "call(absolute_path=%s, statbuf=0x%08x)", pathname.data(), statbuf);

    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    const std::filesystem::path absolute_path(pathname);
    if (is_capio_path(absolute_path)) {
        consent_request_cache_fs->consent_request(pathname, tid, __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

inline int capio_lstat_wrapper(const std::string_view &pathname, struct stat *statbuf, pid_t tid) {
    START_LOG(tid, "call(path=%s, buf=0x%08x)", pathname.data(), statbuf);

    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    const std::filesystem::path absolute_path = capio_posix_realpath(pathname);
    if (absolute_path.empty()) {
        errno = ENOENT;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }
    return capio_lstat(absolute_path.native(), statbuf, tid);
}

inline int capio_fstatat(int dirfd, const std::string_view &pathname, struct stat *statbuf,
                         int flags, pid_t tid) {
    START_LOG(tid, "call(dirfd=%ld, pathname=%s, statbuf=0x%08x, flags=%X)", dirfd, pathname.data(),
              statbuf, flags);

    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    std::filesystem::path path(pathname);
    if (path.empty() && (flags & AT_EMPTY_PATH) == AT_EMPTY_PATH) {
        if (dirfd == AT_FDCWD) {
            // operate on currdir
            return capio_lstat(get_current_dir().native(), statbuf, tid);
        }
        // operate on dirfd. in this case dirfd can refer to any type of file
        return capio_fstat(dirfd, statbuf, tid);
    }
    if (path.is_relative()) {
        if (dirfd == AT_FDCWD) {
            // pathname is interpreted relative to currdir
            return capio_lstat_wrapper(path.native(), statbuf, tid);
        }
        if (!is_directory(dirfd)) {
            errno = ENOTDIR;
            return CAPIO_POSIX_SYSCALL_ERRNO;
        }
        const std::filesystem::path dir_path = get_dir_path(dirfd);
        if (dir_path.empty()) {
            return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
        }
        path = (dir_path / path).lexically_normal();
        return capio_lstat(path.native(), statbuf, tid);
    }
    return capio_lstat(path.native(), statbuf, tid);
}

#if defined(SYS_fstat) || defined(SYS_fstat64)
int fstat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd   = static_cast<int>(arg0);
    auto *buf = reinterpret_cast<struct stat *>(arg1);
    auto tid  = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    return posix_return_value(capio_fstat(fd, buf, tid), result);
}
#endif // SYS_fstat || SYS_fstat64

#if defined(SYS_fstatat) || defined(SYS_newfstatat) || defined(SYS_fstatat64)
int fstatat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    auto dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    auto *statbuf = reinterpret_cast<struct stat *>(arg2);
    auto flags    = static_cast<int>(arg3);
    auto tid      = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    return posix_return_value(capio_fstatat(dirfd, pathname, statbuf, flags, tid), result);
}
#endif // SYS_fstatat || SYS_newfstatat || SYS_fstatat64

#if defined(SYS_lstat) || defined(SYS_lstat64)
int lstat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    auto *buf = reinterpret_cast<struct stat *>(arg1);
    auto tid  = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    return posix_return_value(capio_lstat_wrapper(pathname, buf, tid), result);
}
#endif // SYS_lstat || SYS_lstat64

#if defined(SYS_stat) || defined(SYS_stat64)
int stat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    auto *buf = reinterpret_cast<struct stat *>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_lstat_wrapper(pathname, buf, tid), result);
}
#endif // SYS_stat || SYS_stat64

#endif // CAPIO_POSIX_HANDLERS_STAT_HPP