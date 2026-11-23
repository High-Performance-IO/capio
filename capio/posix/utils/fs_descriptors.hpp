#ifndef CAPIO_POSIX_UTILS_FS_DESCRIPTORS_HPP
#define CAPIO_POSIX_UTILS_FS_DESCRIPTORS_HPP

#include <cstdio>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>

#include "filesystem.hpp"
#include "requests.hpp"

#if !CAPIO_MEM_STREAM

/*
 * Descriptors of the FS variant: real descriptors of real files.
 *
 * The rule is act first, announce second: the server hears of a close once close(2) has returned.
 * On NFS that is when the data reaches the server and when a full disk is reported, and a file
 * must not be committed before the former or after the latter. What cannot be announced afterwards
 * (the exit of the process) is flushed first with close(dup(fd)), which is what close does, without
 * a sync to the disk.
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
    if (exists_capio_fd(fd)) {
        delete_capio_fd(fd);
    }
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

inline void fs_announce_close(const FsClose &closing, const long tid) {
    if (!closing.path.empty()) {
        close_request_fs(closing.path, closing.error, tid);
    }
}

inline int fs_close(const int fd, const long tid, long *result) {
    FsClose closing = fs_prepare_close(fd);
    // Forget it before closing: another thread may get the same number as soon as it is free
    delete_capio_fd(fd);
    *result = syscall_no_intercept(SYS_close, fd);
    if (*result == -EBADF) {
        return CAPIO_POSIX_SYSCALL_SUCCESS; // there was nothing to close
    }
    if (*result < 0 && *result != -EINTR) { // EINTR: Linux has closed it
        closing.error = -*result;
    }
    fs_announce_close(closing, tid);
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

/// @brief The exit of the process: the kernel closes everything, when nothing can announce it
inline void fs_process_exit(const long tid) {
    for (const int fd : get_capio_fds()) {
        FsClose closing = fs_prepare_close(fd);
        fs_flush(closing);
        fs_announce_close(closing, tid);
    }
}

inline bool fs_is_last_thread() {
    struct stat tasks{};
    // a directory has a link for each subdirectory, plus two
    return syscall_no_intercept(SYS_newfstatat, AT_FDCWD, "/proc/self/task", &tasks, 0) == 0 &&
           tasks.st_nlink <= 3;
}

/// @brief Find the descriptors of CAPIO files that a program inherits when it starts
inline void fs_track_inherited(const long tid) {
    if (std::getenv("CAPIO_DIR") == nullptr) {
        return;
    }
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
        if (fd_flags < 0) {
            continue; // the descriptor of the listing itself
        }
        fs_track(tid, path, fd, 0, (fd_flags & FD_CLOEXEC) != 0);
        // Opened outside CAPIO (a posix_spawn file action, a shell that is not preloaded): if
        // nobody registered as its producer, the program that writes it is
        const long status = syscall_no_intercept(SYS_fcntl, fd, F_GETFL);
        if (status >= 0 && (status & O_ACCMODE) != O_RDONLY) {
            inherit_request_fs(path, tid);
        }
    }
    syscall_no_intercept_flag = false;
}

#endif // !CAPIO_MEM_STREAM
#endif // CAPIO_POSIX_UTILS_FS_DESCRIPTORS_HPP
