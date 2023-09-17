#ifndef CAPIO_POSIX_HANDLERS_OPENAT_HPP
#define CAPIO_POSIX_HANDLERS_OPENAT_HPP

#include "globals.hpp"
#include "utils/filesystem.hpp"
#include "lseek.hpp"

inline int capio_openat(int dirfd, const char* pathname, int flags, bool is_creat, long tid) {

    CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: enter\n", tid, dirfd, pathname, flags, is_creat);

    std::string path_to_check;
    if(is_absolute(pathname)) {
        path_to_check = pathname;

        CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: absolute path %s\n", tid, dirfd, pathname, flags, is_creat, path_to_check.c_str());
    }
    else {
        if(dirfd == AT_FDCWD) {
            path_to_check = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
            if (path_to_check.length() == 0) {
              CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: AT_FDCWD invalid path, return -2\n", tid, dirfd, pathname, flags, is_creat);
              return -2;
            }

            CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: AT_FDCWD %s\n", tid, dirfd, pathname, flags, is_creat, path_to_check.c_str());
        }
        else {
            if (is_directory(dirfd) != 1) {
              CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: not a directory, return -2\n", tid, dirfd, pathname, flags, is_creat);
              return -2;
            }
            std::string dir_path;
            auto it = capio_files_descriptors->find(dirfd);
            if (it == capio_files_descriptors->end())
                dir_path = get_dir_path(pathname, dirfd);
            else
                dir_path = it->second;
            if (dir_path.length() == 0) {
                CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: invalid path, return -2\n", tid, dirfd, pathname, flags, is_creat);
                return -2;
            }
            std::string pathstr = pathname;
            if (pathstr.substr(0, 2) == "./") {
                path_to_check = dir_path + pathstr.substr(1, pathstr.length() - 1);

                CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: path modified %s\n", tid, dirfd, pathname, flags, is_creat, pathname);
            }
            else if (std::string(pathname) == ".") {
                path_to_check = dir_path;

            }
            else if (std::string(pathname) == "..") {
                path_to_check = get_capio_parent_dir(dir_path);
            }
            else {
                path_to_check = dir_path + "/" + pathname;
            }
            CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: with dirfd path_to_check %s dir_path %s\n", tid, dirfd, pathname, flags, is_creat, path_to_check.c_str(), dir_path.c_str());
        }
    }
    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());

    CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: CAPIO_DIR is %s\n", tid, dirfd, pathname, flags, is_creat, capio_dir->c_str());

    if (res.first == capio_dir->end()) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd == -1) {
            err_exit("capio_open, /dev/null opening", "capio_openat");
        }
        if (!is_creat)
            is_creat = (flags & O_CREAT) == O_CREAT;
        bool excl = (flags & O_EXCL) == O_EXCL;
        if (excl) {
            CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: adding create_exclusive_request\n", tid, dirfd, pathname, flags, is_creat);
            off64_t return_code = create_exclusive_request(fd, pathname, tid);
            if (return_code == 1) {
                errno = EEXIST;
                CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: O_EXCL and file already exists, return -1\n", tid, dirfd, pathname, flags, is_creat);
                return -1;
            }
        } else if (is_creat) {
            CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: adding create_request\n", tid, dirfd, pathname, flags, is_creat);
            create_request(fd, pathname, tid);
        } else {
            CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: adding open_request\n", tid, dirfd, pathname, flags, is_creat);
            open_request(fd, pathname, tid);
        }
        off64_t* p_offset = (off64_t*) create_shm("offset_" + std::to_string(tid) + "_" + std::to_string(fd), sizeof(off64_t));
        *p_offset = 0;
        off64_t init_size = DEFAULT_FILE_INITIAL_SIZE;
        if ((flags & O_DIRECTORY) == O_DIRECTORY)
            flags = flags | O_LARGEFILE;
        if ((flags & O_CLOEXEC) == O_CLOEXEC) {
            CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: open with O_CLOEXEC\n", tid, dirfd, pathname, flags, is_creat);
            flags &= ~O_CLOEXEC;
            files->insert({fd, std::make_tuple(p_offset, &init_size, flags, FD_CLOEXEC)});
        }
        else
            files->insert({fd, std::make_tuple(p_offset, &init_size, flags, 0)});
        (*capio_files_descriptors)[fd] = path_to_check;
        capio_files_paths->insert(path_to_check);
        if ((flags & O_APPEND) == O_APPEND) {
            CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: calling capio_lseek\n", tid, dirfd, pathname, flags, is_creat);
            capio_lseek(fd, 0, SEEK_END, tid);
        }

        CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: return %d\n", tid, dirfd, pathname, flags, is_creat, fd);
        return fd;
    }
    else {

        CAPIO_DBG("capio_openat TID[%ld] DIRFD[%d] PATHNAME[%s] FLAGS[%d] IS_CREAT[%d]: file not found, return -2\n", tid, dirfd, pathname, flags, is_creat);

        return -2;
    }
}

int openat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    int dirfd = static_cast<int>(arg0);
    const char *pathname = reinterpret_cast<const char *>(arg1);
    int flags = static_cast<int>(arg2);
    int res = capio_openat(dirfd, pathname, flags, false, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_OPENAT_HPP
