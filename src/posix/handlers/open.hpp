#ifndef CAPIO_POSIX_HANDLERS_OPENAT_HPP
#define CAPIO_POSIX_HANDLERS_OPENAT_HPP

#include "lseek.hpp"

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
        int fd = static_cast<int>(syscall_no_intercept(SYS_open, "/dev/null", O_RDONLY));
        if (fd == -1) {
            ERR_EXIT("capio_open, /dev/null opening");
        }
        bool create = (flags & O_CREAT) == O_CREAT;
        bool excl   = (flags & O_EXCL) == O_EXCL;
        if (excl) {
            off64_t return_code = create_exclusive_request(fd, path, tid);
            if (return_code == 1) {
                errno = EEXIST;
                return CAPIO_POSIX_SYSCALL_ERRNO;
            }
        } else if (create) {
            off64_t return_code = create_request(fd, path, tid);
            if (return_code == 1) {
                errno = ENOENT;
                return CAPIO_POSIX_SYSCALL_ERRNO;
            }
        } else {
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
            capio_lseek(fd, 0, SEEK_END, tid);
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

int openat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    int flags = static_cast<int>(arg2);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_openat(dirfd, pathname, flags, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_OPENAT_HPP
