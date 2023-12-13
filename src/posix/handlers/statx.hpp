#ifndef CAPIO_POSIX_HANDLERS_STATX_HPP
#define CAPIO_POSIX_HANDLERS_STATX_HPP

#include "utils/functions.hpp"

inline void fill_statxbuf(struct statx *statxbuf, off_t file_size, bool is_dir, ino_t inode,
                          int mask) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(filesize=%ld, is_dir=%d, inode=%d, mask=%d)",
              file_size, static_cast<int>(is_dir), mask);

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

inline int capio_statx(int dirfd, const std::string *pathname, int flags, int mask,
                       struct statx *statxbuf, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%d, mask=%d, statxbuf=0x%08x)", dirfd,
              pathname->c_str(), flags, mask, statxbuf);

    std::string absolute_path = *pathname;
    if ((flags & AT_EMPTY_PATH) == AT_EMPTY_PATH) {
        if (dirfd == AT_FDCWD) { // operate on currdir
            absolute_path = get_current_dir_name();
        } else { // operate on dirfd. in this case dirfd can refer to any type of file
            if (!pathname->empty()) {
                if (exists_capio_fd(dirfd)) {
                    absolute_path = get_capio_fd_path(dirfd);
                } else {
                    LOG("returning -2 due to !exists_capio_fd");
                    return POSIX_REQUEST_SYSCALL_TO_HANDLE_BY_KERNEL;
                }
            } else {
                LOG("returning -1 due to pathname empty");
                // TODO: set errno
                return POSIX_SYSCALL_HANDLED_BY_CAPIO_SET_ERRNO;
            }
        }
    } else {
        if (!is_absolute(pathname)) {
            if (dirfd == AT_FDCWD) {
                LOG("dirfd is AT_FDCWD");
                absolute_path = *capio_posix_realpath(pathname);
                if (absolute_path.empty()) {
                    LOG("returning -1 due to pathname empty");
                    return POSIX_SYSCALL_HANDLED_BY_CAPIO_SET_ERRNO;
                }
            } else {
                if (!is_directory(dirfd)) {
                    LOG("returning -2 due to !is_directory");
                    return POSIX_REQUEST_SYSCALL_TO_HANDLE_BY_KERNEL;
                }
                std::string dir_path = get_dir_path(dirfd);
                if (dir_path.empty()) {
                    LOG("returning -2 due to dir path empty");
                    return POSIX_REQUEST_SYSCALL_TO_HANDLE_BY_KERNEL;
                }
                if (pathname->substr(0, 2) == "./") {
                    absolute_path = pathname->substr(2, pathname->length() - 1);
                }
                if (absolute_path.at(absolute_path.length() - 1) == '.') {
                    absolute_path = dir_path;
                } else {
                    absolute_path = dir_path + "/" + absolute_path;
                }
            }
        }
        if (!is_capio_path(absolute_path)) {
            LOG("returning -2 due to not being a capio path");
            return POSIX_REQUEST_SYSCALL_TO_HANDLE_BY_KERNEL;
        }
    }

    auto [file_size, is_dir] = stat_request(absolute_path, tid);
    LOG("Filling statx buffer");
    fill_statxbuf(statxbuf, file_size, is_dir, std::hash<std::string>{}(absolute_path), mask);
    return POSIX_SYSCALL_HANDLED_BY_CAPIO;
}

int statx_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    auto flags = static_cast<int>(arg2);
    auto mask  = static_cast<int>(arg3);
    auto *buf  = reinterpret_cast<struct statx *>(arg4);
    long tid   = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(dirfd=%ld, pathname=%s, flags=%d, mask=%d)", dirfd, pathname.c_str(),
              flags, mask);

    return posix_return_value(capio_statx(dirfd, &pathname, flags, mask, buf, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_STATX_HPP
