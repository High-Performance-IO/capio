#ifndef CAPIO_POSIX_HANDLERS_STAT_HPP
#define CAPIO_POSIX_HANDLERS_STAT_HPP

#include <sys/vfs.h>

#include "capio/env.hpp"

#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"

inline blkcnt_t get_nblocks(off64_t file_size) {
    return (file_size % 4096 == 0) ? (file_size / 512) : (file_size / 512 + 8);
}

inline void fill_statbuf(struct stat *statbuf, off_t file_size, bool is_dir, ino_t inode) {
    START_LOG(syscall_no_intercept(SYS_gettid),
              "call(statbuf=0x%08x, file_size=%ld, is_dir=%s, inode=%ul)", statbuf, file_size,
              is_dir ? "true" : "false", inode);

    struct timespec time {
        1, 1
    };
    if (is_dir == 1) {
        statbuf->st_mode = S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
        file_size        = 4096;
    } else {
        statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    }
    statbuf->st_dev     = 100;
    statbuf->st_ino     = inode;
    statbuf->st_nlink   = 1;
    statbuf->st_uid     = syscall_no_intercept(SYS_getuid);
    statbuf->st_gid     = syscall_no_intercept(SYS_getgid);
    statbuf->st_rdev    = 0;
    statbuf->st_size    = file_size;
    statbuf->st_blksize = 4096;
    statbuf->st_blocks  = (file_size < 4096) ? 8 : get_nblocks(file_size);
    statbuf->st_atim    = time;
    statbuf->st_mtim    = time;
    statbuf->st_ctim    = time;
}

inline int capio_fstat(int fd, struct stat *statbuf, long tid) {
    START_LOG(tid, "call(fd=%d, statbuf=0x%08x)", fd, statbuf);

    if (exists_capio_fd(fd)) {
        auto [file_size, is_dir] = fstat_request(fd, tid);
        if (file_size == -1) {
            errno = ENOENT;
            return POSIX_SYSCALL_ERRNO;
        }
        fill_statbuf(statbuf, file_size, is_dir, std::hash<std::string>{}(get_capio_fd_path(fd)));
        return POSIX_SYSCALL_SUCCESS;
    } else {
        return POSIX_SYSCALL_REQUEST_SKIP;
    }
}

inline int capio_lstat(const std::filesystem::path &absolute_path, struct stat *statbuf, long tid) {
    START_LOG(tid, "call(absolute_path=%s, statbuf=0x%08x)", absolute_path.c_str(), statbuf);

    if (is_capio_path(absolute_path)) {
        auto [file_size, is_dir] = stat_request(absolute_path, tid);
        if (file_size == -1) {
            errno = ENOENT;
            return POSIX_SYSCALL_ERRNO;
        }
        fill_statbuf(statbuf, file_size, is_dir, std::hash<std::string>{}(absolute_path));
        return POSIX_SYSCALL_SUCCESS;
    } else {
        return POSIX_SYSCALL_REQUEST_SKIP;
    }
}

inline int capio_lstat_wrapper(const std::filesystem::path &path, struct stat *statbuf, long tid) {
    START_LOG(tid, "call(path=%s, buf=0x%08x)", path.c_str(), statbuf);

    const std::filesystem::path absolute_path = capio_posix_realpath(path);
    if (absolute_path.empty()) {
        errno = ENOENT;
        return POSIX_SYSCALL_ERRNO;
    }
    return capio_lstat(absolute_path, statbuf, tid);
}

inline int capio_fstatat(int dirfd, std::filesystem::path &pathname, struct stat *statbuf,
                         int flags, long tid) {
    START_LOG(tid, "call(dirfd=%ld, pathname=%s, statbuf=0x%08x, flags=%X)", dirfd,
              pathname.c_str(), statbuf, flags);

    if (pathname.empty() && (flags & AT_EMPTY_PATH) == AT_EMPTY_PATH) {
        if (dirfd == AT_FDCWD) { // operate on currdir
            return capio_lstat(get_current_dir(), statbuf, tid);
        } else { // operate on dirfd. in this case dirfd can refer to any type of file
            return capio_fstat(dirfd, statbuf, tid);
        }
    } else if (pathname.is_relative()) {
        if (dirfd == AT_FDCWD) {
            // pathname is interpreted relative to currdir
            return capio_lstat_wrapper(pathname, statbuf, tid);
        } else {
            if (!is_directory(dirfd)) {
                errno = ENOTDIR;
                return POSIX_SYSCALL_ERRNO;
            }
            const std::filesystem::path dir_path = get_dir_path(dirfd);
            if (dir_path.empty()) {
                return POSIX_SYSCALL_REQUEST_SKIP;
            }
            pathname = (dir_path / pathname).lexically_normal();
            return capio_lstat(pathname, statbuf, tid);
        }
    } else {
        return capio_lstat(pathname, statbuf, tid);
    }
}

int fstat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd   = static_cast<int>(arg0);
    auto *buf = reinterpret_cast<struct stat *>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_fstat(fd, buf, tid), result);
}

int fstatat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    auto dirfd = static_cast<int>(arg0);
    std::filesystem::path pathname(reinterpret_cast<const char *>(arg1));
    auto *statbuf = reinterpret_cast<struct stat *>(arg2);
    auto flags    = static_cast<int>(arg3);
    long tid      = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_fstatat(dirfd, pathname, statbuf, flags, tid), result);
}

int lstat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::filesystem::path path(reinterpret_cast<const char *>(arg0));
    auto *buf = reinterpret_cast<struct stat *>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_lstat(path, buf, tid), result);
}

int stat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::filesystem::path path(reinterpret_cast<const char *>(arg0));
    auto *buf = reinterpret_cast<struct stat *>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_lstat_wrapper(path, buf, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_STAT_HPP
