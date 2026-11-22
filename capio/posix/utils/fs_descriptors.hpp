#ifndef CAPIO_POSIX_UTILS_FS_DESCRIPTORS_HPP
#define CAPIO_POSIX_UTILS_FS_DESCRIPTORS_HPP

#include <cstdio>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>

#include "filesystem.hpp"
#include "fs_listing.hpp"
#include "requests.hpp"

#if !CAPIO_MEM_STREAM

/*
 * Descriptors of the FS variant: real descriptors of real files.
 *
 * The rule is act first, announce second: the server hears of a close once close(2) has returned.
 * On NFS that is when the data reaches the server and when a full disk is reported, and a file
 * must not be committed before the former or after the latter. What cannot be announced afterwards
 * (exit, dup2 over a descriptor, close_range) is flushed first with close(dup(fd)), which is what
 * close does, without a sync to the disk.
 */

/// @brief What an announcement needs to know of a descriptor, taken before it is closed
struct FsClose {
    int fd = -1;
    std::string path; ///< Empty if there is nothing to announce: a directory or a removed file
    bool writable = false;
    long error    = 0;
};

/// @brief The physical path of @p fd, empty if it has none
inline std::string fs_descriptor_link(const int fd) {
    char link[64], target[PATH_MAX];
    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    const long length = syscall_no_intercept(SYS_readlinkat, AT_FDCWD, link, target, PATH_MAX);
    if (length <= 0 || target[0] != '/') {
        return {};
    }
    std::string path(target, length);
    constexpr std::string_view removed = " (deleted)";
    if (path.size() > removed.size() &&
        path.compare(path.size() - removed.size(), removed.size(), removed) == 0) {
        return {};
    }
    return path;
}

/// @brief Register @p fd as a descriptor of @p path, forgetting what had that number before
inline void fs_track(const long tid, const std::string &path, const int fd, const int flags,
                     const bool is_cloexec) {
    if (path.empty()) {
        return; // dup of a descriptor that another thread has closed
    }
    const long offset = syscall_no_intercept(SYS_lseek, fd, 0, SEEK_CUR);
    CAPIO_DESCRIPTOR_TABLE_LOCK();
    if (exists_capio_fd(fd)) {
        delete_capio_fd(fd);
    }
    fs_forget_listing(fd);
    add_capio_fd(tid, path, fd, offset < 0 ? 0 : offset, CAPIO_DEFAULT_FILE_INITIAL_SIZE, flags,
                 is_cloexec);
}

/// @brief Describe the close of the CAPIO descriptor @p fd, which is still open
inline FsClose fs_prepare_close(const int fd) {
    FsClose closing;
    closing.fd = fd;
    struct stat open_file{}, named_file{};
    if (syscall_no_intercept(SYS_fstat, fd, &open_file) != 0 || S_ISDIR(open_file.st_mode)) {
        return closing;
    }
    const long flags = syscall_no_intercept(SYS_fcntl, fd, F_GETFL);
    closing.writable = flags >= 0 && (flags & O_ACCMODE) != O_RDONLY;

    // The path it was opened with, unless another process renamed the file since
    const std::string opened_as = get_capio_fd_path(fd);
    if (!opened_as.empty() &&
        syscall_no_intercept(SYS_newfstatat, AT_FDCWD, opened_as.c_str(), &named_file, 0) == 0 &&
        named_file.st_ino == open_file.st_ino && named_file.st_dev == open_file.st_dev) {
        closing.path = opened_as;
    } else if (auto path = fs_descriptor_link(fd); !path.empty() && is_capio_path(path)) {
        closing.path = std::move(path);
    }
    return closing;
}

/// @brief Push the data written through a descriptor that is about to be closed implicitly
inline void fs_flush(FsClose &closing) {
    if (closing.path.empty() || !closing.writable) {
        return;
    }
    long result = syscall_no_intercept(SYS_dup, closing.fd);
    if (result >= 0) {
        result = syscall_no_intercept(SYS_close, result);
    } else {
        result = syscall_no_intercept(SYS_fdatasync, closing.fd); // no descriptor left to dup to
    }
    if (result < 0 && result != -EINTR) {
        closing.error = -result;
    }
}

inline void fs_announce_close(const FsClose &closing, const char kind, const long tid) {
    if (!closing.path.empty()) {
        close_request_fs(closing.path, kind, closing.writable, closing.error, tid);
    }
}

inline int fs_close(const int fd, const long tid, long *result) {
    FsClose closing = fs_prepare_close(fd);
    fs_forget_listing(fd);
    // Forget it before closing: another thread may get the same number as soon as it is free
    delete_capio_fd(fd);
    *result = syscall_no_intercept(SYS_close, fd);
    if (*result == -EBADF) {
        return CAPIO_POSIX_SYSCALL_SUCCESS; // there was nothing to close
    }
    if (*result < 0 && *result != -EINTR) { // EINTR: Linux has closed it
        closing.error = -*result;
    }
    fs_announce_close(closing, CAPIO_FS_CLOSE_EXPLICIT, tid);
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

inline int fs_dup(const int fd, const long tid, long *result) {
    if (!exists_capio_fd(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    *result = syscall_no_intercept(SYS_dup, fd);
    if (*result >= 0) {
        fs_track(tid, get_capio_fd_path(fd), static_cast<int>(*result), 0, false);
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

/// @brief dup2 and dup3, which close @p new_fd implicitly
inline int fs_dup_onto(const int old_fd, const int new_fd, const int flags, const bool is_dup3,
                       const long tid, long *result) {
    const bool old_is_capio = exists_capio_fd(old_fd);
    const bool new_is_capio = exists_capio_fd(new_fd);
    if (old_fd == new_fd || (!old_is_capio && !new_is_capio)) {
        return CAPIO_POSIX_SYSCALL_SKIP; // nothing changes for CAPIO
    }
    FsClose replaced;
    if (new_is_capio && syscall_no_intercept(SYS_fcntl, old_fd, F_GETFD) >= 0) {
        replaced = fs_prepare_close(new_fd);
        fs_flush(replaced);
    }
#ifdef SYS_dup2
    *result = is_dup3 ? syscall_no_intercept(SYS_dup3, old_fd, new_fd, flags)
                      : syscall_no_intercept(SYS_dup2, old_fd, new_fd);
#else
    *result = syscall_no_intercept(SYS_dup3, old_fd, new_fd, flags);
#endif
    if (*result < 0) {
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    if (new_is_capio) {
        delete_capio_fd(new_fd);
        fs_announce_close(replaced, CAPIO_FS_CLOSE_IMPLICIT, tid);
    }
    if (old_is_capio) {
        fs_track(tid, get_capio_fd_path(old_fd), new_fd, 0, (flags & O_CLOEXEC) == O_CLOEXEC);
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

inline int fs_fcntl(const int fd, const int cmd, const long arg, const long tid, long *result) {
    *result = syscall_no_intercept(SYS_fcntl, fd, cmd, arg);
    if (*result < 0) {
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
        fs_track(tid, get_capio_fd_path(fd), static_cast<int>(*result), 0, cmd == F_DUPFD_CLOEXEC);
    } else if (cmd == F_SETFD) {
        set_capio_fd_cloexec(fd, (arg & FD_CLOEXEC) == FD_CLOEXEC);
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

/// @brief The flag of close_range that only sets close-on-exec
constexpr unsigned FS_CLOSE_RANGE_CLOEXEC = 1U << 2;

inline int fs_close_range(const unsigned first, const unsigned last, const unsigned flags,
                          const long tid, long *result) {
    std::vector<int> affected;
    for (const int fd : get_capio_fds()) {
        if (static_cast<unsigned>(fd) >= first && static_cast<unsigned>(fd) <= last) {
            affected.push_back(fd);
        }
    }
    if (affected.empty()) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    const bool only_cloexec = (flags & FS_CLOSE_RANGE_CLOEXEC) != 0;
    std::vector<FsClose> closing;
    if (!only_cloexec) {
        for (const int fd : affected) {
            closing.push_back(fs_prepare_close(fd));
            fs_flush(closing.back());
        }
    }
    *result = syscall_no_intercept(SYS_close_range, first, last, flags);
    if (*result < 0) {
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    for (const int fd : affected) {
        if (only_cloexec) {
            set_capio_fd_cloexec(fd, true);
        } else {
            delete_capio_fd(fd);
        }
    }
    for (const auto &closed : closing) {
        fs_announce_close(closed, CAPIO_FS_CLOSE_IMPLICIT, tid);
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

/*
 * execve does not return when it works, so what it closes is sent before it as pending, and the
 * server counts it when the new program registers. If execve returns, it failed.
 */
inline int fs_execve(const long path, const long argv, const long envp, const long tid,
                     long *result) {
    stop_requests_of_other_threads(tid);
    bool has_pending = false;
    for (const int fd : get_capio_fds()) {
        const long fd_flags = syscall_no_intercept(SYS_fcntl, fd, F_GETFD);
        if (fd_flags >= 0 && (fd_flags & FD_CLOEXEC) != 0) {
            const FsClose closing = fs_prepare_close(fd);
            fs_announce_close(closing, CAPIO_FS_CLOSE_AT_EXEC, tid);
            has_pending |= !closing.path.empty();
        }
    }
    *result = syscall_no_intercept(SYS_execve, path, argv, envp);
    if (has_pending) {
        close_request_fs("-", CAPIO_FS_EXEC_FAILED, false, 0, tid);
    }
    resume_requests();
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

/// @brief The exit of the process: the kernel closes everything, when nothing can announce it
inline void fs_process_exit(const long tid) {
    stop_requests_of_other_threads(tid);
    for (const int fd : get_capio_fds()) {
        FsClose closing = fs_prepare_close(fd);
        fs_flush(closing);
        fs_announce_close(closing, CAPIO_FS_CLOSE_IMPLICIT, tid);
    }
    close_request_fs("-", CAPIO_FS_PROCESS_EXIT, false, 0, tid);
}

inline bool fs_is_last_thread() {
    struct stat tasks{};
    // a directory has a link for each subdirectory, plus two
    return syscall_no_intercept(SYS_newfstatat, AT_FDCWD, "/proc/self/task", &tasks, 0) == 0 &&
           tasks.st_nlink <= 3;
}

/**
 * Rename, and on success tell the server, which moves what it knows of the file to the new name.
 * renameat2 can exchange two paths: that moves no file to a new life, so it is not announced.
 */
inline int fs_rename(const std::filesystem::path &from, const std::filesystem::path &to,
                     const long from_dir, const long from_arg, const long to_dir, const long to_arg,
                     const long flags, const long tid, long *result) {
    const bool from_capio = is_capio_path(from);
    const bool to_capio   = is_capio_path(to);
    if (!from_capio && !to_capio) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    *result = syscall_no_intercept(SYS_renameat2, from_dir, from_arg, to_dir, to_arg, flags);

    constexpr long exchange = 1 << 1; // RENAME_EXCHANGE
    if (*result < 0 || from == to || (flags & exchange) != 0) {
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    if (from_capio) {
        // The descriptors of the file, or of what is inside the directory, follow it
        const auto &old_name = from.native();
        for (const int fd : get_capio_fds()) {
            const std::string path = get_capio_fd_path(fd);
            if (path.compare(0, old_name.size(), old_name) != 0 ||
                (path.size() > old_name.size() && path[old_name.size()] != '/')) {
                continue;
            }
            delete_capio_fd(fd);
            if (to_capio) {
                fs_track(tid, to.native() + path.substr(old_name.size()), fd, 0, false);
            }
        }
    }
    rename_request_fs(from, from_capio, to, to_capio, tid);
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

/// @brief Find the descriptors of CAPIO files that a program inherits when it starts
inline void fs_track_inherited(const long tid) {
    protect_descriptor_table_across_fork();
    if (std::getenv("CAPIO_DIR") == nullptr) {
        return;
    }
    get_capio_dir(); // set it now, before there are threads to race for it
    syscall_no_intercept_flag = true;
    std::vector<int> open_fds;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd", error)) {
        open_fds.push_back(std::atoi(entry.path().filename().c_str()));
    }
    for (const int fd : open_fds) {
        const auto path = fs_descriptor_link(fd);
        if (path.empty() || !is_capio_path(path)) {
            continue;
        }
        const long fd_flags = syscall_no_intercept(SYS_fcntl, fd, F_GETFD);
        if (fd_flags >= 0) { // not the descriptor of the listing itself
            fs_track(tid, path, fd, 0, (fd_flags & FD_CLOEXEC) != 0);
        }
    }
    syscall_no_intercept_flag = false;
}

#endif // !CAPIO_MEM_STREAM
#endif // CAPIO_POSIX_UTILS_FS_DESCRIPTORS_HPP
