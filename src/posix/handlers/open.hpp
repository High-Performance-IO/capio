#ifndef CAPIO_POSIX_HANDLERS_OPENAT_HPP
#define CAPIO_POSIX_HANDLERS_OPENAT_HPP

#include "globals.hpp"
#include "lseek.hpp"
#include "utils/filesystem.hpp"

inline int capio_openat(int dirfd, std::string *pathname, int flags, long tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%X)", dirfd, pathname->c_str(), flags);

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
                LOG("dirfd does not point to a directory");
                return -2;
            }
            std::string dir_path = get_dir_path(dirfd);
            if (dir_path.length() == 0) {
                return -2;
            }

            if (pathname->substr(0, 2) == "./") {
                path_to_check = dir_path + pathname->substr(1, pathname->length() - 1);
            } else if (*pathname == ".") {
                path_to_check = dir_path;
            } else if (*pathname == "..") {
                path_to_check = get_capio_parent_dir(dir_path);
            } else {
                path_to_check = dir_path + "/" + *pathname;
            }
        }
    }
    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());

    if (res.first == capio_dir->end()) {
        int fd = static_cast<int>(syscall_no_intercept(SYS_open, "/dev/null", O_RDONLY));
        if (fd == -1) {
            ERR_EXIT("capio_open, /dev/null opening");
        }
        bool create = (flags & O_CREAT) == O_CREAT;
        bool excl = (flags & O_EXCL) == O_EXCL;
        if (excl) {
            off64_t return_code = create_exclusive_request(fd, path_to_check, tid);
            if (return_code == 1) {
                errno = EEXIST;
                return -1;
            }
        } else if (create) {
            off64_t return_code = create_request(fd, path_to_check, tid);
            if (return_code == 1) {
                errno = ENOENT;
                return -1;
            }
        } else {
            off64_t return_code = open_request(fd, path_to_check, tid);
            if (return_code == 1) {
                errno = ENOENT;
                return -1;
            }
        }
        syscall_no_intercept_flag = true;
        off64_t *p_offset = (off64_t *)create_shm(
            "offset_" + std::to_string(tid) + "_" + std::to_string(fd), sizeof(off64_t));
        syscall_no_intercept_flag = false;
        *p_offset = 0;
        off64_t init_size = DEFAULT_FILE_INITIAL_SIZE;
        int actual_flags = flags;
        if ((flags & O_DIRECTORY) == O_DIRECTORY) {
            actual_flags = actual_flags | O_LARGEFILE;
        }
        if ((flags & O_CLOEXEC) == O_CLOEXEC) {
            actual_flags &= ~O_CLOEXEC;
            files->insert({fd, std::make_tuple(p_offset, &init_size, actual_flags, FD_CLOEXEC)});
        } else {
            files->insert({fd, std::make_tuple(p_offset, &init_size, actual_flags, 0)});
        }
        (*capio_files_descriptors)[fd] = path_to_check;
        capio_files_paths->insert(path_to_check);
        if ((flags & O_APPEND) == O_APPEND) {
            capio_lseek(fd, 0, SEEK_END, tid);
        }
        return fd;
    } else {
        return -2;
    }
}

int creat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string pathname(reinterpret_cast<const char *>(arg0));
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(pathname=%s)", pathname.c_str());

    int res = capio_openat(AT_FDCWD, &pathname, O_CREAT | O_WRONLY | O_TRUNC, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

int openat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int dirfd = static_cast<int>(arg0);
    std::string pathname(reinterpret_cast<const char *>(arg1));
    int flags = static_cast<int>(arg2);
    long tid = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%X)", dirfd, pathname.c_str(), flags);

    int res = capio_openat(dirfd, &pathname, flags, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_OPENAT_HPP
