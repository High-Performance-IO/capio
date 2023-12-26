#ifndef CAPIO_POSIX_HANDLERS_MKDIR_HPP
#define CAPIO_POSIX_HANDLERS_MKDIR_HPP

#include "utils/common.hpp"
#include "utils/filesystem.hpp"

inline off64_t capio_mkdirat(int dirfd, std::filesystem::path &pathname, mode_t mode, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, mode=%o)", dirfd, pathname.c_str(), mode);

    if (pathname.is_relative()) {
        if (dirfd == AT_FDCWD) {
            pathname = capio_posix_realpath(pathname);
            if (pathname.empty()) {
                return POSIX_SYSCALL_REQUEST_SKIP;
            }
        } else {
            if (!is_directory(dirfd)) {
                return POSIX_SYSCALL_REQUEST_SKIP;
            }
            const std::filesystem::path dir_path = get_dir_path(dirfd);
            if (dir_path.empty()) {
                return POSIX_SYSCALL_REQUEST_SKIP;
            }
            pathname = dir_path / pathname;
        }
    }

    if (is_capio_path(pathname)) {
        if (exists_capio_path(pathname)) {
            errno = EEXIST;
            return POSIX_SYSCALL_ERRNO;
        }
        off64_t res = mkdir_request(pathname, tid);
        if (res == 1) {
            return POSIX_SYSCALL_ERRNO;
        } else {
            LOG("Adding %s to capio_files_path", pathname.c_str());
            add_capio_path(pathname);
            return res;
        }
    } else {
        return POSIX_SYSCALL_REQUEST_SKIP;
    }
}

inline off64_t capio_rmdir(std::filesystem::path &pathname, long tid) {
    START_LOG(tid, "call(pathname=%s)", pathname.c_str());

    if (pathname.is_relative()) {
        pathname = capio_posix_realpath(pathname);
        if (pathname.empty()) {
            LOG("path_to_check.len = 0!");
            return POSIX_SYSCALL_REQUEST_SKIP;
        }
    }

    if (is_capio_path(pathname)) {
        if (!exists_capio_path(pathname)) {
            LOG("capio_files_path.find == end. errno = "
                "ENOENT");
            errno = ENOENT;
            return POSIX_SYSCALL_ERRNO;
        }
        off64_t res = rmdir_request(pathname, tid);
        if (res == 2) {
            LOG("res == 2. errno = ENOENT");
            errno = ENOENT;
            return POSIX_SYSCALL_ERRNO;
        } else {
            delete_capio_path(pathname);
            return res;
        }
    } else {
        return POSIX_SYSCALL_REQUEST_SKIP;
    }
}

int mkdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::filesystem::path pathname(reinterpret_cast<const char *>(arg0));
    auto mode = static_cast<mode_t>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_mkdirat(AT_FDCWD, pathname, mode, tid), result);
}

int mkdirat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    int dirfd = static_cast<int>(arg0);
    std::filesystem::path pathname(reinterpret_cast<const char *>(arg1));
    auto mode = static_cast<mode_t>(arg2);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_mkdirat(dirfd, pathname, mode, tid), result);
}

int rmdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::filesystem::path pathname(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_rmdir(pathname, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_MKDIR_HPP
