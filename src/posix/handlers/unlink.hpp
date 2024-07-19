#ifndef CAPIO_POSIX_HANDLERS_UNLINK_HPP
#define CAPIO_POSIX_HANDLERS_UNLINK_HPP

#if defined(SYS_unlink) || defined(SYS_unlinkat)

#include "utils/common.hpp"

off64_t capio_unlink_abs(const std::filesystem::path &abs_path, long tid, bool is_dir) {
    START_LOG(tid, "call(abs_path=%s, is_dir=%s)", abs_path.c_str(), is_dir ? "true" : "false");

    if (is_capio_path(abs_path)) {
        LOG("Removing %s from capio_files_path", abs_path.c_str());
        delete_capio_path(abs_path);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

inline off64_t capio_unlinkat(int dirfd, const std::string_view &pathname, int flags, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%X)", dirfd, pathname.data(), flags);

    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    off64_t res;
    bool is_dir = flags & AT_REMOVEDIR;
    std::filesystem::path path(pathname);
    if (path.is_relative()) {
        if (dirfd == AT_FDCWD) {
            path = capio_posix_realpath(path);
            if (path.empty()) {
                return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
            }
            res = capio_unlink_abs(path, tid, is_dir);
        } else {
            if (!is_directory(dirfd)) {
                return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
            }
            const std::filesystem::path dir_path = get_dir_path(dirfd);
            if (dir_path.empty()) {
                return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
            }
            path = (dir_path / path).lexically_normal();
            res  = capio_unlink_abs(path, tid, is_dir);
        }
    } else {
        res = capio_unlink_abs(path, tid, is_dir);
    }

    return res;
}

int unlink_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string_view pathname(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(path=%s)", pathname.data());

    if (is_capio_path(pathname)) {
        LOG("Deleting path");
        delete_capio_path(pathname.data());
    }

    return CAPIO_POSIX_SYSCALL_SKIP;
}

int unlinkat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    int dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    int flags = static_cast<int>(arg2);
    long tid  = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(path=%s)", pathname.data());
    auto path = capio_posix_realpath(pathname);
    if (is_capio_path(path)) {
        LOG("Deleting path");
        delete_capio_path(path);
    }

    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_unlink || SYS_unlinkat
#endif // CAPIO_POSIX_HANDLERS_UNLINK_HPP
