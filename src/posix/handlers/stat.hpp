#ifndef CAPIO_POSIX_HANDLERS_STAT_HPP
#define CAPIO_POSIX_HANDLERS_STAT_HPP

#include <sys/vfs.h>

#include "capio/env.hpp"
#include "globals.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"

inline int capio_fstat(int fd, struct stat *statbuf, long tid) {
    START_LOG(tid, "call(fd=%d, statbuf=0x%08x)", fd, statbuf);

    auto it = files->find(fd);
    if (it != files->end()) {
        auto [file_size, is_dir] = fstat_request(fd, tid);
        statbuf->st_dev          = 100;
        std::hash<std::string> hash;
        statbuf->st_ino = hash((*capio_files_descriptors)[fd]);
        if (is_dir == 0) {
            statbuf->st_mode =
                S_IFDIR | S_IRWXU | S_IWGRP | S_IRGRP | S_IXOTH | S_IROTH; // 0755 directory
            file_size = 4096;
        } else {
            statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0666 regular file
        }
        statbuf->st_nlink   = 1;
        statbuf->st_uid     = syscall_no_intercept(SYS_getuid);
        statbuf->st_gid     = syscall_no_intercept(SYS_getgid);
        statbuf->st_rdev    = 0;
        statbuf->st_size    = file_size;
        statbuf->st_blksize = 4096;
        if (file_size < 4096) {
            statbuf->st_blocks = 8;
        } else {
            statbuf->st_blocks = get_nblocks(file_size);
        }
        struct timespec time;
        time.tv_sec      = 1;
        time.tv_nsec     = 1;
        statbuf->st_atim = time;
        statbuf->st_mtim = time;
        statbuf->st_ctim = time;
        return 0;
    } else {
        return -2;
    }
}

inline int capio_lstat(const std::string &absolute_path, struct stat *statbuf, long tid) {
    START_LOG(tid, "call(absolute_path=%s, statbuf=0x%08x)", absolute_path.c_str(), statbuf);

    const std::string *capio_dir = get_capio_dir();
    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), absolute_path.begin());
    if (res.first == capio_dir->end()) {
        if (capio_dir->size() == absolute_path.size()) {
            // it means capio_dir is equals to absolute_path
            return -2;
        }
        auto [file_size, is_dir] = stat_request(absolute_path, tid);
        statbuf->st_dev          = 100;
        std::hash<std::string> hash;
        statbuf->st_ino = hash(absolute_path);
        if (is_dir == 0) {
            statbuf->st_mode =
                S_IFDIR | S_IRWXU | S_IWGRP | S_IRGRP | S_IXOTH | S_IROTH; // 0755 directory
            file_size = 4096;
        } else {
            statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0644 regular file
        }
        statbuf->st_nlink   = 1;
        statbuf->st_uid     = getuid();
        statbuf->st_gid     = getgid();
        statbuf->st_rdev    = 0;
        statbuf->st_size    = file_size;
        statbuf->st_blksize = 4096;
        if (file_size < 4096) {
            statbuf->st_blocks = 8;
        } else {
            statbuf->st_blocks = get_nblocks(file_size);
        }
        struct timespec time {
            1 /* tv_sec */, 1 /* tv_nsec */
        };
        statbuf->st_atim = time;
        statbuf->st_mtim = time;
        statbuf->st_ctim = time;
        return 0;
    } else {
        return -2;
    }
}

inline int capio_lstat_wrapper(const std::string *path, struct stat *statbuf, long tid) {
    START_LOG(tid, "call(path=%s, buf=0x%08x)", path, statbuf);

    if (path == nullptr) {
        return -2;
    }
    const std::string *capio_dir = get_capio_dir();

    const std::string *absolute_path = capio_posix_realpath(tid, path, capio_dir, current_dir);
    if (absolute_path->length() == 0) {
        return -2;
    }
    return capio_lstat(*absolute_path, statbuf, tid);
}

inline int capio_fstatat(int dirfd, std::string *pathname, struct stat *statbuf, int flags,
                         long tid) {
    START_LOG(tid, "call(dirfd=%ld, pathname=%s, statbuf=0x%08x, flags=%X)", dirfd,
              pathname->c_str(), statbuf, flags);

    if ((flags & AT_EMPTY_PATH) == AT_EMPTY_PATH) {
        if (dirfd == AT_FDCWD) { // operate on currdir
            std::string path(get_current_dir_name());
            return capio_lstat(path, statbuf, tid);
        } else { // operate on dirfd. in this case dirfd can refer to any type
            // of file
            if (pathname->length() == 0) {
                return capio_fstat(dirfd, statbuf, tid);
            } else {
                // TODO: set errno
                return -1;
            }
        }
    }

    if (!is_absolute(pathname)) {
        if (dirfd == AT_FDCWD) {
            // pathname is interpreted relative to currdir
            return capio_lstat_wrapper(pathname, statbuf, tid);
        } else {
            if (!is_directory(dirfd)) {
                return -2;
            }
            std::string dir_path = get_dir_path(dirfd);
            ;
            if (dir_path.length() == 0) {
                return -2;
            }

            if (pathname->substr(0, 2) == "./") {
                *pathname = pathname->substr(2, pathname->length() - 1);
            }
            std::string path;
            if (pathname->at(pathname->length() - 1) == '.') {
                path = dir_path;
            } else {
                path = dir_path + "/" + *pathname;
            }
            return capio_lstat(path, statbuf, tid);
        }
    } else {
        return capio_lstat(*pathname, statbuf, tid);
    }
}

inline int capio_fstatfs(int fd, struct statfs *buf, long tid) {
    START_LOG(tid, "call(fd=%d, buf=0x%08x)", fd, buf);

    if (files->find(fd) != files->end()) {
        std::string path             = (*capio_files_descriptors)[fd];
        const std::string *capio_dir = get_capio_dir();

        return statfs(capio_dir->c_str(), buf);
    } else {
        return -2;
    }
}

int fstat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd   = static_cast<int>(arg0);
    auto *buf = reinterpret_cast<struct stat *>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, buf=0x%08x)", fd, buf);

    int res = capio_fstat(fd, buf, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int fstatat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    auto dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    auto *statbuf = reinterpret_cast<struct stat *>(arg2);
    auto flags    = static_cast<int>(arg3);
    long tid      = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(dirfd=%ld, pathname=%s, statbuf=0x%08x, flags=%X)", dirfd,
              pathname.c_str(), statbuf, flags);

    int res = capio_fstatat(dirfd, &pathname, statbuf, flags, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int fstatfs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    auto fd   = static_cast<int>(arg0);
    auto *buf = reinterpret_cast<struct statfs *>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, buf=0x%08x)", fd, buf);

    int res = capio_fstatfs(fd, buf, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int lstat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string path(reinterpret_cast<const char *>(arg0));
    auto *buf = reinterpret_cast<struct stat *>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(path=%s, buf=0x%08x)", path.c_str(), buf);

    int res = capio_lstat_wrapper(&path, buf, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_STAT_HPP