#ifndef CAPIO_POSIX_HANDLERS_UNLINK_HPP
#define CAPIO_POSIX_HANDLERS_UNLINK_HPP

#include "globals.hpp"

off64_t capio_unlink_abs(const std::string &abs_path, long pid, bool is_dir) {
    START_LOG(pid, "call(abs_path=%s, is_dir=%s)", abs_path.c_str(), is_dir? "true" : "false");
    off64_t res;
    const std::string *capio_dir = get_capio_dir();
    auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), abs_path.begin());
    if (it.first == capio_dir->end()) {
        if (capio_dir->size() == abs_path.size()) {
            ERR_EXIT("ERROR: unlink to the capio_dir %s", abs_path.c_str());
        }
        if (is_dir) {
            res = rmdir_request(abs_path.c_str(), pid);
        } else {
            res = unlink_request(abs_path.c_str(), pid);
        }
        if (res == -1)
            errno = ENOENT;
    } else {
        res = -2;
    }

    return res;
}

inline off64_t capio_unlinkat(int dirfd,const std::string& pathname, int flags, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%X)", dirfd, pathname.c_str(), flags);

    const std::string* capio_dir = get_capio_dir();
    if (capio_dir->length() == 0) {
        return -2;
    }
    off64_t res;
    bool is_dir = flags & AT_REMOVEDIR;
    if (!is_absolute(&pathname)) {
        if (dirfd == AT_FDCWD) {
            const std::string* abs_path = capio_posix_realpath(tid, &pathname, capio_dir, current_dir);
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

int unlink_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result){
    std::string pathname(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(pathname=%s)", pathname.c_str());

    off64_t res = capio_unlinkat(AT_FDCWD, pathname, 0, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int unlinkat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result){
    int dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    int flags = static_cast<int>(arg2);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%X)", dirfd, pathname.c_str(), flags);

    off64_t res = capio_unlinkat(dirfd, pathname, flags, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }

    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_UNLINK_HPP
