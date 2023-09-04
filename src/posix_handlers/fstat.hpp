#ifndef CAPIO_FSTAT_HPP
#define CAPIO_FSTAT_HPP
int capio_fstat(int fd,struct stat *statbuf,long my_tid) {
    auto it = files->find(fd);
    if (it != files->end()) {

        CAPIO_DBG("capio_fstat captured\n");

        char c_str[256];
        sprintf(c_str, "fsta %ld %d", my_tid, fd);
        buf_requests->write(c_str, 256 * sizeof(char));

        CAPIO_DBG("capio_fstat captured after write\n");

        off64_t file_size;
        off64_t is_dir;
        (*bufs_response)[my_tid]->read(&file_size);
        (*bufs_response)[my_tid]->read(&is_dir);
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

        CAPIO_DBG("capio_fstat file_size=%ld is_dir %d\n", file_size, is_dir);

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
        return 0;
    } else
        return -2;

}

int fstat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    struct stat *buf = reinterpret_cast<struct stat *>(arg1);

    CAPIO_DBG("fstat %ld\n", syscall_no_intercept(SYS_gettid));

    int res = capio_fstat(static_cast<int>(arg0), buf, my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}


#endif //CAPIO_FSTAT_HPP
