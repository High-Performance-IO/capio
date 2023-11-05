#ifndef CAPIO_POSIX_HANDLERS_STATX_HPP
#define CAPIO_POSIX_HANDLERS_STATX_HPP

inline void fill_statxbuf(struct statx *statxbuf, off_t file_size, bool is_dir, ino_t inode,
                          int mask) {
    statx_timestamp time{1, 1};
    if (is_dir == 0) {
        statxbuf->stx_mode |= S_IFDIR;
        statxbuf->stx_mode |= S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
        file_size = 4096;
    } else {
        statxbuf->stx_mode |= S_IFREG;
        statxbuf->stx_mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
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
            if (pathname->length() != 0) {
                if (exists_capio_fd(dirfd)) {
                    absolute_path = get_capio_fd_path(dirfd);
                } else {
                    return -2;
                }
            } else {
                // TODO: set errno
                return -1;
            }
        }
    } else {
        if (!is_absolute(pathname)) {
            if (dirfd == AT_FDCWD) {
                absolute_path = *capio_posix_realpath(tid, pathname);
            } else {
                if (!is_directory(dirfd)) {
                    return -2;
                }
                std::string dir_path = get_dir_path(dirfd);
                if (dir_path.length() == 0) {
                    return -2;
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
            return -2;
        }
    }

    auto [file_size, is_dir] = stat_request(absolute_path, tid);
    fill_statxbuf(statxbuf, file_size, is_dir, std::hash<std::string>{}(absolute_path), mask);
    return 0;
}

int statx_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    auto flags = static_cast<int>(arg2);
    auto mask  = static_cast<int>(arg3);
    auto *buf  = reinterpret_cast<struct statx *>(arg4);
    long tid   = syscall_no_intercept(SYS_gettid);

    int res = capio_statx(dirfd, &pathname, flags, mask, buf, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_STATX_HPP
