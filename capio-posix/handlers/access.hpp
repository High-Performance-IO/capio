#ifndef CAPIO_POSIX_HANDLERS_ACCESS_HPP
#define CAPIO_POSIX_HANDLERS_ACCESS_HPP

#include "utils/common.hpp"
#include "utils/filesystem.hpp"

#if defined(SYS_access)
inline int access_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result,
                   const pid_t tid) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    START_LOG(tid, "call()");
    if (!is_capio_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    std::filesystem::path path(pathname);
    if (path.is_relative()) {
        path = capio_posix_realpath(pathname);
    }

    consent_request_cache_fs->consent_request(path, tid, __FUNCTION__);
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}
#endif // SYS_access

#if defined(SYS_faccessat) || defined(SYS_faccessat2)
inline int faccessat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result, const pid_t tid) {
    auto dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    START_LOG(tid, "call()");

    if (!is_capio_path(pathname)) {
        LOG("Path %s is forbidden or is not a capio path: skip", pathname.data());
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
        }
    }

    consent_request_cache_fs->consent_request(path, tid, __FUNCTION__);
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}
#endif // SYS_faccessat

#endif // CAPIO_POSIX_HANDLERS_ACCESS_HPP