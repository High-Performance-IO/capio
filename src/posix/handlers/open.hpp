#ifndef CAPIO_POSIX_HANDLERS_OPENAT_HPP
#define CAPIO_POSIX_HANDLERS_OPENAT_HPP

#include "utils/common.hpp"
#include "utils/filesystem.hpp"

std::string compute_abs_path(char *pathname, int dirfd) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(pathname=%s, dirfd=%d)", pathname, dirfd);
    std::filesystem::path path(pathname);
    if (path.is_relative()) {
        if (dirfd == AT_FDCWD) {
            path = capio_posix_realpath(path);
            if (path.empty()) {
                LOG("path empty AT_FDCWD");
                return "";
            }
        } else {
            if (!is_directory(dirfd)) {
                LOG("dirfd does not point to a directory");
                return "";
            }
            const std::filesystem::path dir_path = get_dir_path(dirfd);
            if (dir_path.empty()) {
                LOG("path empty");
                return "";
            }

            path = (dir_path / path).lexically_normal();
            LOG("path = %s", path.c_str());
        }
    }
    return path;
}

#if defined(SYS_creat)
int creat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string pathname(reinterpret_cast<const char *>(arg0));
    auto tid    = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    int flags   = O_CREAT | O_WRONLY | O_TRUNC;
    mode_t mode = static_cast<int>(arg2);
    START_LOG(tid, "call(path=%s, flags=%d, mode=%d)", pathname.data(), flags, mode);

    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    std::string path = compute_abs_path(pathname.data(), -1);

    if (is_capio_path(path)) {
        create_request(-1, path.data(), tid);
        LOG("Create request sent");
    }

    int fd = static_cast<int>(syscall_no_intercept(SYS_creat, arg0, arg1, arg2, arg3, arg4, arg5));

    LOG("fd=%d", fd);

    if (is_capio_path(path) && fd >= 0) {
        LOG("Registering path and fd");
        add_capio_fd(tid, path, fd, 0, (flags & O_CLOEXEC) == O_CLOEXEC);
    }

    *result = fd;
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}
#endif // SYS_creat

#if defined(SYS_open)
int open_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string pathname(reinterpret_cast<const char *>(arg0));
    int flags   = static_cast<int>(arg1);
    mode_t mode = static_cast<int>(arg2);
    auto tid    = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    START_LOG(tid, "call(path=%s, flags=%d, mode=%d)", pathname.data(), flags, mode);

    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    std::string path = compute_abs_path(pathname.data(), -1);

    if (is_capio_path(path)) {
        if ((flags & O_CREAT) == O_CREAT) {
            LOG("O_CREAT");
            create_request(-1, path.data(), tid);
        } else {
            LOG("not O_CREAT");
            open_request(-1, path, tid);
        }
    } else {
        LOG("Not a CAPIO path. skipping...");
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    char resolved_path[PATH_MAX];
    syscall_no_intercept(SYS_readlink, path.c_str(), resolved_path, PATH_MAX);
    LOG("Resolved symlink path: %s", resolved_path);

    /*
     * Here it might happen that we try to open a symbolic link. instead of opening the link, we
     * open the resolved link. in this way, when we get the associated path from the File
     * descriptor, we ensure that the file descriptor is associated to the real file. This way the
     * server always check on the real file and not on the link
     */

    int fd = static_cast<int>(syscall_no_intercept(SYS_open, reinterpret_cast<long>(resolved_path),
                                                   arg1, arg2, arg3, arg4, arg5));

    if (is_capio_path(path) && fd >= 0) {
        LOG("Adding capio path");
        add_capio_fd(tid, resolved_path, fd, 0, (flags & O_CLOEXEC) == O_CLOEXEC);
    }

    *result = fd;
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}
#endif // SYS_open

#if defined(SYS_openat)
int openat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    int flags   = static_cast<int>(arg2);
    mode_t mode = static_cast<int>(arg3);
    auto tid    = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    START_LOG(tid, "call(dirfd=%ld, path=%s, flags=%d, mode=%d)", dirfd, pathname.data(), flags,
              mode);

    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    std::string path = compute_abs_path(pathname.data(), dirfd);

    if (is_capio_path(path)) {
        if ((flags & O_CREAT) == O_CREAT) {
            LOG("O_CREAT");
            create_request(-1, path.data(), tid);
        } else {
            LOG("not O_CREAT");
            open_request(-1, path.data(), tid);
        }
    } else {
        LOG("Not a CAPIO path. skipping...");
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    char resolved_path[PATH_MAX];
    syscall_no_intercept(SYS_readlinkat, arg0, path.c_str(), resolved_path, PATH_MAX);
    LOG("Resolved symlink path: %s", resolved_path);

    /**
     * Using readlinkat, we have the realpath of the symbolic link, and we can perform a normal open
     */
    int fd = static_cast<int>(syscall_no_intercept(SYS_open, reinterpret_cast<long>(resolved_path),
                                                   arg1, arg2, arg3, arg4, arg5));
    LOG("fd=%d", fd);

    if (is_capio_path(path) && fd >= 0) {
        LOG("Adding capio path");
        add_capio_fd(tid, resolved_path, fd, 0, (flags & O_CLOEXEC) == O_CLOEXEC);
    }

    *result = fd;
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}
#endif // SYS_openat

#endif // CAPIO_POSIX_HANDLERS_OPENAT_HPP