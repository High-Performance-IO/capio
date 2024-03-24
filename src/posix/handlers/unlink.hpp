#ifndef CAPIO_POSIX_HANDLERS_UNLINK_HPP
#define CAPIO_POSIX_HANDLERS_UNLINK_HPP

#include "utils/common.hpp"

off64_t capio_unlink_abs(const std::filesystem::path &abs_path, long tid, bool is_dir) {
    START_LOG(tid, "call(abs_path=%s, is_dir=%s)", abs_path.c_str(), is_dir ? "true" : "false");

    if (is_capio_path(abs_path)) {
        if (is_capio_dir(abs_path)) {
            ERR_EXIT("ERROR: unlink to the capio_dir %s", abs_path.c_str());
        } else {
            off64_t res = is_dir ? rmdir_request(abs_path, tid) : unlink_request(abs_path, tid);
            if (res == -1) {
                errno = ENOENT;
            } else {
                LOG("Removing %s from capio_files_path", abs_path.c_str());
                delete_capio_path(abs_path);
            }
            return res;
        }
    } else {
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }
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
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_unlinkat(AT_FDCWD, pathname, 0, tid), result);
}

int unlinkat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    int dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    int flags = static_cast<int>(arg2);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_unlinkat(dirfd, pathname, flags, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_UNLINK_HPP
