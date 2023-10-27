#ifndef CAPIO_POSIX_HANDLERS_MKDIR_HPP
#define CAPIO_POSIX_HANDLERS_MKDIR_HPP

#include "globals.hpp"
#include "utils/filesystem.hpp"

inline off64_t capio_mkdirat(int dirfd, std::string *pathname, mode_t mode, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, mode=%o)", dirfd, pathname->c_str(), mode);

    const std::string *capio_dir = get_capio_dir();
    std::string path_to_check(*pathname);
    if (!is_absolute(pathname)) {
        if (dirfd == AT_FDCWD) {
            path_to_check = *capio_posix_realpath(tid, pathname, capio_dir, current_dir);
            if (path_to_check.length() == 0) {
                return -2;
            }
        } else {
            if (!is_directory(dirfd)) {
                return -2;
            }
            std::string dir_path = get_dir_path(dirfd);
            if (dir_path.length() == 0) {
                return -2;
            }
            path_to_check = dir_path + "/" + *pathname;
        }
    }
    auto res_mismatch = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());
    if (res_mismatch.first == capio_dir->end()) {
        if (capio_dir->size() == path_to_check.size()) {
            return -2;
        } else {
            if (capio_files_paths->find(path_to_check) != capio_files_paths->end()) {
                errno = EEXIST;
                return -1;
            }
            off64_t res = mkdir_request(path_to_check, tid);
            if (res == 1) {
                return -1;
            } else {
                LOG("Adding %s to capio_files_path", path_to_check.c_str());
                capio_files_paths->insert(path_to_check);
                return res;
            }
        }
    } else {
        LOG("File %s already present in capio_files_path", path_to_check.c_str());
        return -2;
    }
}

inline off64_t capio_rmdir(std::string *pathname, long tid) {
    START_LOG(tid, "call(pathname=%s)", pathname->c_str());

    const std::string *capio_dir = get_capio_dir();
    std::string path_to_check(*pathname);
    if (!is_absolute(pathname)) {
        path_to_check = *capio_posix_realpath(tid, pathname, capio_dir, current_dir);
        if (path_to_check.length() == 0) {
            LOG("path_to_check.len = 0!");
            return -2;
        }
    }
    auto res_mismatch = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());
    if (res_mismatch.first == capio_dir->end()) {
        if (capio_dir->size() == path_to_check.size()) {
            LOG("capio_dir.size == path_to_check.size");
            return -2;
        } else {
            if (capio_files_paths->find(path_to_check) == capio_files_paths->end()) {
                LOG("capio_files_path.find == end. errno = "
                    "ENOENT");
                errno = ENOENT;
                return -1;
            }
            off64_t res = rmdir_request(path_to_check.c_str(), tid);
            if (res == 2) {
                LOG("res == 2. errno = ENOENT");
                errno = ENOENT;
                return -1;
            } else {
                capio_files_paths->erase(path_to_check);
                return res;
            }
        }
    } else {
        LOG("generic return -2");
        return -2;
    }
}

int mkdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string pathname(reinterpret_cast<const char *>(arg0));
    auto mode = static_cast<mode_t>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(pathname=%s, mode=%o)", pathname.c_str(), mode);

    off64_t res = capio_mkdirat(AT_FDCWD, &pathname, mode, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int mkdirat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    int dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    auto mode = static_cast<mode_t>(arg2);
    long tid  = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(dirfd=%d, pathname=%s, mode=%o)", dirfd, pathname.c_str(), mode);

    off64_t res = capio_mkdirat(dirfd, &pathname, mode, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int rmdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string pathname(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(directory=%s)", pathname.c_str());

    off64_t res = capio_rmdir(&pathname, tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_MKDIR_HPP
