#ifndef CAPIO_POSIX_HANDLERS_MKDIR_HPP
#define CAPIO_POSIX_HANDLERS_MKDIR_HPP

#if defined(SYS_mkdir) || defined(SYS_mkdirat) || defined(SYS_rmdir)

#include "utils/common.hpp"
#include "utils/filesystem.hpp"

inline off64_t capio_mkdirat(int dirfd, const std::string_view &pathname, mode_t mode, pid_t tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, mode=%o)", dirfd, pathname.data(), mode);

    if (is_forbidden_path(pathname)) {
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

int mkdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    auto mode = static_cast<mode_t>(arg1);
    auto tid  = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    return posix_return_value(capio_mkdirat(AT_FDCWD, pathname, mode, tid), result);
}

int mkdirat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    int dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    auto mode = static_cast<mode_t>(arg2);
    auto tid  = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    return posix_return_value(capio_mkdirat(dirfd, pathname, mode, tid), result);
}

int rmdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    auto tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    return posix_return_value(capio_rmdir(pathname, tid), result);
}

#endif // SYS_mkdir || SYS_mkdirat || SYS_rmdir
#endif // CAPIO_POSIX_HANDLERS_MKDIR_HPP
