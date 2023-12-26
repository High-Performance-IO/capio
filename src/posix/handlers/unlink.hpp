#ifndef CAPIO_POSIX_HANDLERS_UNLINK_HPP
#define CAPIO_POSIX_HANDLERS_UNLINK_HPP

#include "utils/common.hpp"

off64_t capio_unlink_abs(const std::filesystem::path &abs_path, long tid, bool is_dir) {
    START_LOG(tid, "call(abs_path=%s, is_dir=%s)", abs_path.c_str(), is_dir ? "true" : "false");
    if (!is_capio_path(abs_path)) {
        if (is_capio_dir(abs_path)) {
            ERR_EXIT("ERROR: unlink to the capio_dir %s", abs_path.c_str());
        } else {
            return POSIX_SYSCALL_REQUEST_SKIP;
        }
    } else {
        off64_t res = is_dir ? rmdir_request(abs_path, tid) : unlink_request(abs_path, tid);
        if (res == -1) {
            errno = ENOENT;
        }
        return res;
    }
}

inline off64_t capio_unlinkat(int dirfd, std::filesystem::path &pathname, int flags, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%X)", dirfd, pathname.c_str(), flags);

    off64_t res;
    bool is_dir = flags & AT_REMOVEDIR;
    if (pathname.is_relative()) {
        if (dirfd == AT_FDCWD) {
            pathname = capio_posix_realpath(pathname);
            if (pathname.empty()) {
                return POSIX_SYSCALL_REQUEST_SKIP;
            }
            res = capio_unlink_abs(pathname, tid, is_dir);
        } else {
            if (!is_directory(dirfd)) {
                return POSIX_SYSCALL_REQUEST_SKIP;
            }
            const std::filesystem::path dir_path = get_dir_path(dirfd);
            if (dir_path.empty()) {
                return POSIX_SYSCALL_REQUEST_SKIP;
            }
            const std::filesystem::path path = (dir_path / pathname).lexically_normal();
            res                              = capio_unlink_abs(path, tid, is_dir);
        }
    } else {
        res = capio_unlink_abs(pathname, tid, is_dir);
    }

    return res;
}

int unlink_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::filesystem::path pathname(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_unlinkat(AT_FDCWD, pathname, 0, tid), result);
}

int unlinkat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    int dirfd = static_cast<int>(arg0);
    std::filesystem::path pathname(reinterpret_cast<const char *>(arg1));
    int flags = static_cast<int>(arg2);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_unlinkat(dirfd, pathname, flags, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_UNLINK_HPP
