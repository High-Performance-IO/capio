#ifndef CAPIO_POSIX_HANDLERS_LSTAT_HPP
#define CAPIO_POSIX_HANDLERS_LSTAT_HPP

#include "globals.hpp"
#include "utils/filesystem.hpp"

inline int capio_lstat(std::string absolute_path,struct stat *statbuf,long tid) {

    CAPIO_DBG("capio_lstat TID[%ld] ABSPATH[%d]: file not found, return -1\n", tid, absolute_path.c_str());

    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), absolute_path.begin());
    if (res.first == capio_dir->end()) {
        if (capio_dir->size() == absolute_path.size()) {
            //it means capio_dir is equals to absolute_path
            CAPIO_DBG("capio_lstat TID[%ld] ABSPATH[%d]: points to CAPIO_DIR, return -2\n", tid, absolute_path.c_str());
            return -2;
        }

        CPStatResponse_t response = stat_request(absolute_path.c_str(), tid);
        off64_t file_size = std::get<0>(response);
        off64_t is_dir = std::get<1>(response);
        statbuf->st_dev = 100;

        std::hash<std::string> hash;
        statbuf->st_ino = hash(absolute_path);

        CAPIO_DBG("capio_lstat TID[%ld] ABSPATH[%d]: file_size %ld, is_dir %d\n", tid, absolute_path.c_str(), file_size, is_dir);

        if (is_dir == 0) {
            statbuf->st_mode = S_IFDIR | S_IRWXU | S_IWGRP | S_IRGRP | S_IXOTH | S_IROTH; // 0755 directory
            file_size = 4096;
        } else
            statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0644 regular file
        statbuf->st_nlink = 1;
        statbuf->st_uid = getuid();
        statbuf->st_gid = getgid();
        statbuf->st_rdev = 0;
        statbuf->st_size = file_size;

        statbuf->st_blksize = 4096;
        if (file_size < 4096)
            statbuf->st_blocks = 8;
        else
            statbuf->st_blocks = get_nblocks(file_size);
        struct timespec time{1 /* tv_sec */, 1 /* tv_nsec */};
        statbuf->st_atim = time;
        statbuf->st_mtim = time;
        statbuf->st_ctim = time;

        CAPIO_DBG("capio_lstat TID[%ld] ABSPATH[%d]: return 0\n", tid, absolute_path.c_str(), file_size, is_dir);

        return 0;
    } else {
        CAPIO_DBG("capio_lstat TID[%ld] ABSPATH[%d]: file not found, return -2\n", tid, absolute_path.c_str());
        return -2;
    }

}

inline int capio_lstat_wrapper(const char *path, struct stat *statbuf, long tid) {

    CAPIO_DBG("capio_lstat_wrapper TID[%ld] PATH[%d]: enter\n", tid, path);

    if (path == nullptr) {
        CAPIO_DBG("capio_lstat_wrapper TID[%ld] PATH[%d]: null path, return -2\n", tid, path);
        return -2;
    }
    std::string absolute_path;
    absolute_path = create_absolute_path(path, capio_dir, current_dir, stat_enabled);
    if (absolute_path.length() == 0) {
        CAPIO_DBG("capio_lstat_wrapper TID[%ld] PATH[%d]: invalid absolute path, return -2\n", tid, path);
        return -2;
    }

    CAPIO_DBG("capio_lstat_wrapper TID[%ld] PATH[%d]: delegate to capio_lstat\n", tid, path);

    return capio_lstat(absolute_path,statbuf,tid);
}


int lstat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result,long tid){

    const char *path = reinterpret_cast<const char *>(arg0);
    auto *buf = reinterpret_cast<struct stat *>(arg1);
    int res = capio_lstat_wrapper(path, buf, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_LSTAT_HPP
