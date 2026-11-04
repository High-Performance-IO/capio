#ifndef CAPIO_POSIX_HANDLERS_FCHMOD_HPP
#define CAPIO_POSIX_HANDLERS_FCHMOD_HPP

#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"

/*
 * CAPIO keeps no permissions or owners, so changing them on a CAPIO file succeeds without doing
 * anything. chmod, fchmodat, chown and fchownat name the file by a path, resolved like access
 * resolves it: a CAPIO path that exists succeeds, one that does not fails with ENOENT, and any
 * other path goes to the kernel.
 */
inline off64_t capio_ignore_path_metadata(int dirfd, const char *pathname, int flags, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%X)", dirfd, pathname, flags);

    if (pathname[0] == '\0' && (flags & AT_EMPTY_PATH)) {
        return exists_capio_fd(dirfd) ? 0 : CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }
    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname);
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    std::filesystem::path path(pathname);
    if (path.is_relative()) {
        if (dirfd == AT_FDCWD) {
            path = capio_posix_realpath(path);
        } else if (is_directory(dirfd)) {
            const std::filesystem::path dir_path = get_dir_path(dirfd);
            if (dir_path.empty()) {
                return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
            }
            path = (dir_path / path).lexically_normal();
        } else {
            return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
        }
    }
    if (path.empty() || !is_capio_path(path)) {
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    const off64_t res = access_request(path, tid);
    if (res == CAPIO_POSIX_SYSCALL_REQUEST_SKIP) {
        return res;
    }
    if (res < 0) {
        LOG("Path %s is not a CAPIO file", path.c_str());
        errno = ENOENT;
        return -1;
    }
    LOG("Path %s is a CAPIO file. Ignoring the operation", path.c_str());
    return 0;
}

int fchmod_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd = static_cast<int>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d)", fd);

    // TODO: Handle mode provided bt arg1

    if (!exists_capio_fd(fd)) {
        LOG("Syscall refers to file not handled by capio. Skipping it!");
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    // Upon success fchmod shall return 0
    // Since capio does not handle permission, we will be
    // Upon the assumption that all the operations occurs with success
    *result = 0;
    LOG("File is present in capio. Ignoring fchmod operation");
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

int chmod_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    return posix_return_value(
        capio_ignore_path_metadata(AT_FDCWD, reinterpret_cast<const char *>(arg0), 0, tid), result);
}

int fchmodat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    // The kernel's fchmodat has no flags argument: fchmodat2 added it.
    return posix_return_value(capio_ignore_path_metadata(static_cast<int>(arg0),
                                                         reinterpret_cast<const char *>(arg1), 0,
                                                         tid),
                              result);
}

#endif // CAPIO_POSIX_HANDLERS_FCHMOD_HPP
