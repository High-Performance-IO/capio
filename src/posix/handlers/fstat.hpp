#ifndef CAPIO_POSIX_HEADERS_FSTAT_HPP
#define CAPIO_POSIX_HEADERS_FSTAT_HPP

#include "globals.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"

inline int capio_fstat(int fd,struct stat *statbuf,long tid) {

  CAPIO_DBG("capio_fstat TID[%ld] FD[%d]: enter\n", tid, fd);

    auto it = files->find(fd);
    if (it != files->end()) {

        CAPIO_DBG("capio_fstat TID[%ld] FD[%d]: add fstat_request\n", tid, fd);

        fstat_request(fd, tid);

        CAPIO_DBG("capio_fstat TID[%ld] FD[%d]: fstat_request added\n", tid, fd);

        off64_t file_size;
        off64_t is_dir;
        (*bufs_response)[tid]->read(&file_size);
        (*bufs_response)[tid]->read(&is_dir);
        statbuf->st_dev = 100;

        std::hash<std::string> hash;
        statbuf->st_ino = hash((*capio_files_descriptors)[fd]);

        if (is_dir == 0) {
            statbuf->st_mode = S_IFDIR | S_IRWXU | S_IWGRP | S_IRGRP | S_IXOTH | S_IROTH; // 0755 directory
            file_size = 4096;
        } else
            statbuf->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0666 regular file
        statbuf->st_nlink = 1;
        statbuf->st_uid = getuid();
        statbuf->st_gid = getgid();
        statbuf->st_rdev = 0;
        statbuf->st_size = file_size;

        CAPIO_DBG("capio_fstat TID[%ld] FD[%d]: file_size is %ld, is_dir is %d\n", tid, fd, file_size, is_dir);

        statbuf->st_blksize = 4096;
        if (file_size < 4096)
            statbuf->st_blocks = 8;
        else
            statbuf->st_blocks = get_nblocks(file_size);

        struct timespec time;
        time.tv_sec = 1;
        time.tv_nsec = 1;
        statbuf->st_atim = time;
        statbuf->st_mtim = time;
        statbuf->st_ctim = time;

        CAPIO_DBG("capio_fstat TID[%ld] FD[%d]: return 0\n", tid, fd);

        return 0;
    } else {
        CAPIO_DBG("capio_fstat TID[%ld] FD[%d]: external file, return -2\n", tid, fd);
        return -2;
    }
}

int fstat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){

    struct stat *buf = reinterpret_cast<struct stat *>(arg1);

    CAPIO_DBG("fstat %ld\n", syscall_no_intercept(SYS_gettid));

    int res = capio_fstat(static_cast<int>(arg0), buf, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}


#endif // CAPIO_POSIX_HEADERS_FSTAT_HPP
