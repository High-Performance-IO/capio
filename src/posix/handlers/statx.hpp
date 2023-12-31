#ifndef CAPIO_POSIX_HANDLERS_STATX_HPP
#define CAPIO_POSIX_HANDLERS_STATX_HPP

#include "utils/common.hpp"

inline void fill_statxbuf(struct statx *statxbuf, off_t file_size, bool is_dir, ino_t inode,
                          int mask) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(filesize=%ld, is_dir=%s, inode=%d, mask=%d)",
              file_size, is_dir ? "true" : "false", mask);

    statx_timestamp time{1, 1};
    if (is_dir == 1) {
        LOG("Filling statx struct for file entry");
        statxbuf->stx_mode = S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
        file_size          = 4096;
    } else {
        LOG("Filling statx struct for directory entry");
        statxbuf->stx_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    }
    statxbuf->stx_mask            = STATX_BASIC_STATS | STATX_BTIME;
    statxbuf->stx_attributes_mask = 0;
    statxbuf->stx_blksize         = 4096;
    statxbuf->stx_nlink           = 1;
    statxbuf->stx_uid             = syscall_no_intercept(SYS_getuid);
    statxbuf->stx_gid             = syscall_no_intercept(SYS_getgid);
    statxbuf->stx_ino             = inode;
    statxbuf->stx_size            = file_size;
    statxbuf->stx_blocks          = (file_size < 4096) ? 8 : get_nblocks(file_size);
    statxbuf->stx_atime           = time;
    statxbuf->stx_btime           = time;
    statxbuf->stx_ctime           = time;
    statxbuf->stx_mtime           = time;
}

inline int capio_statx(int dirfd, const std::string_view &pathname, int flags, int mask,
                       struct statx *statxbuf, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%d, mask=%d, statxbuf=0x%08x)", dirfd,
              pathname.data(), flags, mask, statxbuf);

    if (is_forbidden_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    std::filesystem::path path(pathname);
    if (path.empty() && (flags & AT_EMPTY_PATH) == AT_EMPTY_PATH) {
        LOG("pathname is empty and AT_EMPTY_PATH is set");
        if (dirfd == AT_FDCWD) { // operate on currdir
            LOG("dirfd is AT_FDCWD");
            path = get_current_dir();
        } else { // operate on dirfd. in this case dirfd can refer to any type of file
            if (exists_capio_fd(dirfd)) {
                path = get_capio_fd_path(dirfd);
            } else {
                LOG("returning -2 due to !exists_capio_fd");
                return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
            }
        }
    } else {
        if (path.is_relative()) {
            if (dirfd == AT_FDCWD) {
                LOG("dirfd is AT_FDCWD");
                path = capio_posix_realpath(path);
                if (path.empty()) {
                    LOG("returning -1 due to pathname empty");
                    errno = ENOENT;
                    return CAPIO_POSIX_SYSCALL_ERRNO;
                }
            } else {
                if (!is_directory(dirfd)) {
                    LOG("returning -2 due to !is_directory");
                    errno = ENOTDIR;
                    return CAPIO_POSIX_SYSCALL_ERRNO;
                }
                const std::filesystem::path dir_path = get_dir_path(dirfd);
                if (dir_path.empty()) {
                    LOG("returning -2 due to dir path empty");
                    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
                }
                path = (dir_path / path).lexically_normal();
            }
        }
        if (!is_capio_path(path)) {
            LOG("returning -2 due to not being a capio path");
            return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
        }
    }

    auto [file_size, is_dir] = stat_request(path, tid);
    if (file_size == -1) {
        errno = ENOENT;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }
    fill_statxbuf(statxbuf, file_size, is_dir, std::hash<std::string>{}(path), mask);
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

int statx_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    auto flags = static_cast<int>(arg2);
    auto mask  = static_cast<int>(arg3);
    auto *buf  = reinterpret_cast<struct statx *>(arg4);
    long tid   = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_statx(dirfd, pathname, flags, mask, buf, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_STATX_HPP
