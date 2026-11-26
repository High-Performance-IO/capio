#ifndef CAPIO_POSIX_UTILS_FILESYSTEM_HPP
#define CAPIO_POSIX_UTILS_FILESYSTEM_HPP

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "calf/SyscallLogger.h"
#include "common/env.hpp"
#include "common/path.hpp"
#include "common/syscall.hpp"

#include "file_descriptor_table.hpp"
#include "types.hpp"

inline bool in_dir(const std::string &path, const std::string &glob) {
    const size_t res = path.find('/', glob.length() - 1);
    return res != std::string::npos;
}

inline bool is_directory(int dirfd) {
    START_LOG(capio_syscall(SYS_gettid), "call(dirfd=%d)", dirfd);

    struct stat path_stat{};
    int tmp = fstat(dirfd, &path_stat);
    if (tmp != 0) {
        LOG("Error at is_directory(dirfd=%d) -> %d: %d (%s)", dirfd, tmp, errno,
            std::strerror(errno));
        return -1;
    }
    return S_ISDIR(path_stat.st_mode) == 1;
}

inline bool is_prefix(const std::filesystem::path &path_1, const std::filesystem::path &path_2) {
    const auto relpath = path_2.lexically_relative(path_1);
    return !relpath.empty() && relpath.native().rfind("..", 0) != 0;
}

inline bool is_capio_dir(const std::filesystem::path &path_to_check) {
    START_LOG(capio_syscall(SYS_gettid), "call(path_to_check=%s)", path_to_check.c_str());

    const auto res = get_capio_dir().compare(path_to_check) == 0;
    LOG("is_capio_dir: %s", res ? "yes" : "no");
    return res;
}

inline bool is_capio_path(const std::filesystem::path &path_to_check) {
    START_LOG(capio_syscall(SYS_gettid), "call(path_to_check=%s)", path_to_check.c_str());

    // check if path_to_check begins with CAPIO_DIR
    const auto res = is_prefix(get_capio_dir(), path_to_check);
    LOG("is_capio_path: %s", res ? "yes" : "no");
    return res;
}

/// @brief The CAPIO files of the process. Created by init_filesystem.
inline FileDescriptorTable *fd_table;
std::unique_ptr<std::filesystem::path> current_dir;

/**
 * Get the current directory
 * @return the current directory
 */
inline const std::filesystem::path &get_current_dir() { return *current_dir; }

/**
 * Compute the absolute path for @path.  The form in which CAPIO registers a path: "d/.", "d/" and
 * "d/x/.." all name the directory that mkdir registered as "d", as realpath would return for a path
 * on disk.
 * @param path
 * @return
 */
inline std::filesystem::path capio_normal_path(const std::filesystem::path &path) {
    auto normal = path.lexically_normal().native();
    while (normal.size() > 1 && normal.back() == '/') {
        normal.pop_back();
    }
    return {normal};
}

std::filesystem::path capio_posix_realpath(const std::filesystem::path &pathname) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(path=%s)", pathname.c_str());
    // Relative paths start from CAPIO's working directory, which differs from the kernel's after
    // a chdir into a CAPIO directory. CAPIO paths have no symlinks, so they are resolved
    // lexically; the kernel resolves the others.
    // TODO: support symlinks: a path that reaches the CAPIO directory through one, or through
    //  ".." after one, is not recognised as a CAPIO path.
    const std::filesystem::path absolute =
        capio_normal_path(pathname.is_absolute() ? pathname : *current_dir / pathname);
    if (is_capio_path(absolute)) {
        LOG("Computed CAPIO path = %s", absolute.c_str());
        return absolute;
    }

    const std::unique_ptr<char, decltype(&free)> real_path(
        capio_realpath(absolute.c_str(), nullptr), &free);
    if (real_path == nullptr) {
        LOG("realpath of %s failed: %s", absolute.c_str(), strerror(errno));
        // An absolute path is returned as it is, a relative one that does not resolve is not.
        return pathname.is_absolute() ? absolute : std::filesystem::path{};
    }
    LOG("Computed realpath = %s", real_path.get());
    return {real_path.get()};
}

/**
 * Return the absolute path for the @path argument
 *
 * @param path
 * @return
 */
inline std::filesystem::path capio_absolute(const std::filesystem::path &path) {
    return path.is_absolute() ? capio_normal_path(path) : capio_posix_realpath(path);
}

/**
 * FS variant: flush what was written through @p fd to the file system, as close(2) does, without
 * closing @p fd. For the descriptors that the kernel closes without a close(2) whose result CAPIO
 * sees: at the exit, under dup2/dup3 and close_range.
 *
 * On a network file system such as NFS, close is when the data of a file is sent to the file
 * server, and when a full disk or a quota is reported. A consumer on another node sees only the
 * data flushed before it opens the file. So:
 * - at the exit, CAPIO announces the closes before the kernel makes them: without this flush, the
 *   server could commit a file whose data is still in the cache of the producer's node;
 * - under dup2 and close_range the kernel flushes too, before CAPIO announces the close, but it
 *   drops the error: the flush gets it, so that the server knows that data was lost.
 *
 * It closes a copy of @p fd, which flushes like a close of @p fd itself. If no descriptor is free
 * for the copy, it syncs the data instead, which is slower but flushes too. Nothing to flush for a
 * read-only descriptor.
 *
 * @return the error of the flush, 0 if none
 */
inline long flush_capio_fd(const int fd) {
    if ((fd_table->flags(fd) & O_ACCMODE) == O_RDONLY) {
        return 0;
    }
    long res = syscall_no_intercept(SYS_dup, fd);
    res = res >= 0 ? syscall_no_intercept(SYS_close, res) : syscall_no_intercept(SYS_fdatasync, fd);
    return res < 0 && res != -EINTR ? -res : 0; // EINTR: the copy is closed anyway
}

/**
 * Destroy metadata structures
 * @return
 */
inline void destroy_filesystem() {
    current_dir.reset();
    delete fd_table;
    fd_table = nullptr;
}

/**
 * Get the corresponding path from a dirfd file descriptor
 * @param dirfd
 * @return the corresponding path
 */
std::filesystem::path get_dir_path(int dirfd) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(dirfd=%d)", dirfd);

    if (dirfd == AT_FDCWD) {
        LOG("Returning current CWD");
        return get_current_dir();
    }

    if (const auto path = fd_table->findPath(dirfd)) {
        LOG("dirfd %d points to path %s", dirfd, path->c_str());
        return *path;
    } else {
        char proclnk[128];
        char dir_pathname[PATH_MAX + 1]{}; // readlink does not terminate it
        sprintf(proclnk, "/proc/self/fd/%d", dirfd);
        if (syscall_no_intercept(SYS_readlinkat, AT_FDCWD, proclnk, dir_pathname, PATH_MAX) < 0) {
            LOG("failed to readlink\n");
            return {};
        }
        LOG("dirfd %d points to path %s", dirfd, dir_pathname);
        return {dir_pathname};
    }
}

/**
 * Initialize metadata structures
 * @param tid the thread that started the program: its table is the one it had before an exec, if
 * the program started with one. -1 for an empty table.
 * @return
 */
inline void init_filesystem(const long tid = -1) {
    std::unique_ptr<char[]> buf(new char[PATH_MAX]);
    syscall_no_intercept(SYS_getcwd, buf.get(), PATH_MAX);
    current_dir = std::make_unique<std::filesystem::path>(buf.get());
    fd_table    = tid < 0 ? new FileDescriptorTable() : new FileDescriptorTable(tid);
}

/**
 * Change the current directory
 * @return the current directory
 */
inline void set_current_dir(const std::filesystem::path &cwd) {
    current_dir = std::make_unique<std::filesystem::path>(cwd);
}

#endif // CAPIO_POSIX_UTILS_FILESYSTEM_HPP
