#ifndef CAPIO_POSIX_UTILS_METADATA_HPP
#define CAPIO_POSIX_UTILS_METADATA_HPP

#include <cerrno>
#include <fcntl.h>
#include <filesystem>

#include "common.hpp"
#include "filesystem.hpp"
#include "requests.hpp"

/*
 * The access check behind chmod, fchmodat, chown and fchownat. CAPIO keeps no permissions or
 * owners, so these calls change nothing: they only resolve the path and send an access request.
 * The result is 0 for a CAPIO path the server has a location for, -1 with ENOENT for a CAPIO
 * path it has none for, and CAPIO_POSIX_SYSCALL_REQUEST_SKIP, which gives the call to the kernel,
 * for any other path. With AT_EMPTY_PATH and an empty path, the target is dirfd itself.
 *
 * TODO: keep the mode and owner of CAPIO files, so that these calls change them and stat reports
 * them.
 */
inline off64_t capio_metadata_access(int dirfd, const char *pathname, int flags, long tid) {
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
        LOG("The server has no location for CAPIO path %s", path.c_str());
        errno = ENOENT;
        return -1;
    }
    LOG("Path %s is a CAPIO file. Ignoring the operation", path.c_str());
    return 0;
}

#endif // CAPIO_POSIX_UTILS_METADATA_HPP
