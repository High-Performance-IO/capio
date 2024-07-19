#ifndef CAPIO_POSIX_HANDLERS_OPENAT_HPP
#define CAPIO_POSIX_HANDLERS_OPENAT_HPP

#if defined(SYS_creat) || defined(SYS_open) || defined(SYS_openat)

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

int creat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string pathname(reinterpret_cast<const char *>(arg0));
    long tid    = syscall_no_intercept(SYS_gettid);
    int flags   = O_CREAT | O_WRONLY | O_TRUNC;
    mode_t mode = static_cast<int>(arg2);
    START_LOG(tid, "call(path=%s, flags=%d, mode=%d)", pathname.data(), flags, mode);

    std::string path = compute_abs_path(pathname.data(), -1);

    if (is_capio_path(path)) {
        create_request(-1, path.data(), tid);
        LOG("Create request sent");
    }

    int fd = static_cast<int>(syscall_no_intercept(SYS_creat, arg0, arg1, arg2, arg3, arg4, arg5));

    LOG("fd=%d", fd);

    if (is_capio_path(path) && fd >= 0) {
        LOG("Registering path and fd");
        add_capio_fd(tid, path, fd, 0, CAPIO_DEFAULT_FILE_INITIAL_SIZE, flags,
                     (flags & O_CLOEXEC) == O_CLOEXEC);
    }

    *result = fd;
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

int open_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string pathname(reinterpret_cast<const char *>(arg0));
    int flags   = static_cast<int>(arg1);
    mode_t mode = static_cast<int>(arg2);
    long tid    = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(path=%s, flags=%d, mode=%d)", pathname.data(), flags, mode);

    std::string path = compute_abs_path(pathname.data(), -1);

    if (is_capio_path(path)) {
        if ((flags & O_CREAT) == O_CREAT) {
            LOG("O_CREAT");
            create_request(-1, path.data(), tid);
        } else {
            LOG("not O_CREAT");
            open_request(-1, path.data(), tid);
        }
    }

    int fd = static_cast<int>(syscall_no_intercept(SYS_open, arg0, arg1, arg2, arg3, arg4, arg5));

    if (is_capio_path(path) && fd >= 0) {
        LOG("Adding capio path");
        add_capio_fd(tid, path, fd, 0, CAPIO_DEFAULT_FILE_INITIAL_SIZE, flags,
                     (flags & O_CLOEXEC) == O_CLOEXEC);
    }

    *result = fd;
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

int openat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    int flags   = static_cast<int>(arg2);
    mode_t mode = static_cast<int>(arg3);
    long tid    = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(path=%s, flags=%d, mode=%d)", pathname.data(), flags, mode);

    std::string path = compute_abs_path(pathname.data(), dirfd);

    if (is_capio_path(path)) {
        if ((flags & O_CREAT) == O_CREAT) {
            LOG("O_CREAT");
            create_request(-1, path.data(), tid);
        } else {
            LOG("not O_CREAT");
            open_request(-1, path.data(), tid);
        }
    }

    int fd = static_cast<int>(syscall_no_intercept(SYS_openat, arg0, arg1, arg2, arg3, arg4, arg5));
    LOG("fd=%d", fd);

    if (is_capio_path(path) && fd >= 0) {
        LOG("Adding capio path");
        add_capio_fd(tid, path, fd, 0, CAPIO_DEFAULT_FILE_INITIAL_SIZE, flags,
                     (flags & O_CLOEXEC) == O_CLOEXEC);
    }

    *result = fd;
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // SYS_creat || SYS_open || SYS_openat
#endif // CAPIO_POSIX_HANDLERS_OPENAT_HPP
