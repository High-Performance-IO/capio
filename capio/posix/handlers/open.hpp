#ifndef CAPIO_POSIX_HANDLERS_OPENAT_HPP
#define CAPIO_POSIX_HANDLERS_OPENAT_HPP

#if defined(SYS_creat) || defined(SYS_open) || defined(SYS_openat)

#include "utils/common.hpp"
#include "utils/filesystem.hpp"

inline int capio_openat(int dirfd, const std::string_view &pathname, int flags, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%X)", dirfd, pathname.data(), flags);

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

    if (is_capio_path(path)) {
        int fd =
            static_cast<int>(syscall_no_intercept(SYS_openat, AT_FDCWD, "/dev/null", O_RDONLY));
        if (fd == -1) {
            ERR_EXIT("capio_open, /dev/null opening");
        }
        bool create = (flags & O_CREAT) == O_CREAT;
        bool excl   = (flags & O_EXCL) == O_EXCL;
        if (excl) {
            LOG("excl");
            off64_t return_code = create_exclusive_request(fd, path, tid);
            if (return_code == 1) {
                errno = EEXIST;
                return CAPIO_POSIX_SYSCALL_ERRNO;
            }
        } else if (create) {
            LOG("create");
            off64_t return_code = create_request(fd, path, tid);
            if (return_code == 1) {
                errno = ENOENT;
                return CAPIO_POSIX_SYSCALL_ERRNO;
            }
        } else {
            LOG("!excl && !create");
            off64_t return_code = open_request(fd, path, tid);
            if (return_code == 1) {
                errno = ENOENT;
                return CAPIO_POSIX_SYSCALL_ERRNO;
            }
        }
        int actual_flags = flags & ~O_CLOEXEC;
        if ((flags & O_DIRECTORY) == O_DIRECTORY) {
            actual_flags = actual_flags | O_LARGEFILE;
        }
        add_capio_fd(tid, path, fd, 0, CAPIO_DEFAULT_FILE_INITIAL_SIZE, actual_flags,
                     (flags & O_CLOEXEC) == O_CLOEXEC);
        if ((flags & O_APPEND) == O_APPEND) {
            lseek(fd, 0, SEEK_END);
        }
        return fd;
    } else {
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }
}

int creat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_openat(AT_FDCWD, pathname, O_CREAT | O_WRONLY | O_TRUNC, tid),
                              result);
}

int open_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    int flags = static_cast<int>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_openat(AT_FDCWD, pathname, flags, tid), result);
}

int openat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    int flags = static_cast<int>(arg2);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_openat(dirfd, pathname, flags, tid), result);
}

#ifdef SYS_openat2
#include <linux/openat2.h>

/**
 * Resolve @pathname lexically as openat2 does with RESOLVE_BENEATH or RESOLVE_IN_ROOT, into
 * @resolved, relative to the directory the call starts from. CAPIO files have no symlinks or mount
 * points, so the other RESOLVE_* flags change nothing for them.
 * @return false when the path would leave that directory under RESOLVE_BENEATH: openat2 fails with
 * EXDEV
 */
inline bool capio_openat2_resolve(const std::string_view &pathname, uint64_t resolve,
                                  std::string &resolved) {
    const bool beneath = (resolve & RESOLVE_BENEATH) == RESOLVE_BENEATH;
    const bool in_root = (resolve & RESOLVE_IN_ROOT) == RESOLVE_IN_ROOT;
    if (!beneath && !in_root) {
        resolved = pathname;
        return true;
    }

    std::filesystem::path path(pathname);
    if (path.is_absolute()) {
        if (beneath) {
            return false;
        }
        // RESOLVE_IN_ROOT: "/" is the directory the call starts from.
        path = path.relative_path();
    }
    std::filesystem::path normal;
    for (const auto &part : path) {
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == "..") {
            if (normal.empty()) {
                if (beneath) {
                    return false;
                }
                // RESOLVE_IN_ROOT: ".." stops at the root.
                continue;
            }
            normal = normal.parent_path();
        } else {
            normal /= part;
        }
    }
    resolved = normal.empty() ? "." : normal.string();
    return true;
}

int openat2_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    int dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    const auto *how                  = reinterpret_cast<const struct open_how *>(arg2);
    const auto size                  = static_cast<size_t>(arg3);
    long tid                         = syscall_no_intercept(SYS_gettid);
    constexpr uint64_t known_resolve = RESOLVE_NO_XDEV | RESOLVE_NO_MAGICLINKS |
                                       RESOLVE_NO_SYMLINKS | RESOLVE_BENEATH | RESOLVE_IN_ROOT |
                                       RESOLVE_CACHED;

    START_LOG(tid, "call(dirfd=%d, pathname=%s)", dirfd, pathname.data());

    // Malformed arguments: the kernel answers EFAULT or EINVAL.
    if (how == nullptr || size < sizeof(struct open_how) || (how->resolve & ~known_resolve) != 0 ||
        ((how->resolve & RESOLVE_BENEATH) && (how->resolve & RESOLVE_IN_ROOT))) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    std::string resolved;
    if (!capio_openat2_resolve(pathname, how->resolve, resolved)) {
        errno = EXDEV;
        return posix_return_value(CAPIO_POSIX_SYSCALL_ERRNO, result);
    }

    return posix_return_value(capio_openat(dirfd, resolved, static_cast<int>(how->flags), tid),
                              result);
}
#endif // SYS_openat2

#endif // SYS_creat || SYS_open || SYS_openat
#endif // CAPIO_POSIX_HANDLERS_OPENAT_HPP
