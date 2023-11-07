#ifndef CAPIO_POSIX_HANDLERS_UNLINK_HPP
#define CAPIO_POSIX_HANDLERS_UNLINK_HPP

off64_t capio_unlink_abs(const std::string &abs_path, long tid, bool is_dir) {
    START_LOG(tid, "call(abs_path=%s, is_dir=%s)", abs_path.c_str(), is_dir ? "true" : "false");
    if (!is_capio_path(abs_path)) {
        if (is_capio_dir(abs_path)) {
            ERR_EXIT("ERROR: unlink to the capio_dir %s", abs_path.c_str());
        } else {
            return -2;
        }
    } else {
        off64_t res = is_dir ? rmdir_request(abs_path, tid) : unlink_request(abs_path, tid);
        if (res == -1) {
            errno = ENOENT;
        }
        return res;
    }
}

inline off64_t capio_unlinkat(int dirfd, const std::string &pathname, int flags, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%X)", dirfd, pathname.c_str(), flags);

    off64_t res;
    bool is_dir = flags & AT_REMOVEDIR;
    if (!is_absolute(&pathname)) {
        if (dirfd == AT_FDCWD) {
            const std::string *abs_path = capio_posix_realpath(&pathname);
            if (abs_path->length() == 0) {
                return -2;
            }
            res = capio_unlink_abs(*abs_path, tid, is_dir);
        } else {
            if (!is_directory(dirfd)) {
                return -2;
            }
            std::string dir_path = get_dir_path(dirfd);
            if (dir_path.length() == 0) {
                return -2;
            }
            std::string path = dir_path + "/" + pathname;

            res = capio_unlink_abs(path, tid, is_dir);
        }
    } else {
        res = capio_unlink_abs(pathname, tid, is_dir);
    }

    return res;
}

int unlink_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string pathname(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);

    off64_t res = capio_unlinkat(AT_FDCWD, pathname, 0, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int unlinkat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    int dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    int flags = static_cast<int>(arg2);
    long tid  = syscall_no_intercept(SYS_gettid);

    off64_t res = capio_unlinkat(dirfd, pathname, flags, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }

    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_UNLINK_HPP
