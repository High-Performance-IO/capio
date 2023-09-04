#ifndef CAPIO_LSTAT_HPP
#define CAPIO_LSTAT_HPP


int capio_lstat(std::string absolute_path,struct stat *statbuf,long my_tid) {


    CAPIO_DBG("capio_lstat %s\n", absolute_path.c_str());

    char c_str[256];
    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), absolute_path.begin());
    if (res.first == capio_dir->end()) {
        if (capio_dir->size() == absolute_path.size()) {
            //it means capio_dir is equals to absolute_path
            return -2;

        }

        CAPIO_DBG("capio_lstat sending msg to server\n");

        sprintf(c_str, "stat %ld %s", my_tid, absolute_path.c_str());
        buf_requests->write(c_str, 256 * sizeof(char));

        CAPIO_DBG("capio_lstat after sent msg to server: %s\n", c_str);

        off64_t file_size;
        off64_t is_dir;
        (*bufs_response)[my_tid]->read(&file_size); //TODO: these two reads don't work in multithreading
        if (file_size == -1) {
            errno = ENOENT;
            return -1;
        }
        (*bufs_response)[my_tid]->read(&is_dir);
        statbuf->st_dev = 100;


        std::hash<std::string> hash;
        statbuf->st_ino = hash(absolute_path);

        CAPIO_DBG("lstat isdir %ld\n", is_dir);

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

        CAPIO_DBG("lstat file_size=%ld\n",file_size);

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
    } else {

        CAPIO_DBG("capio_lstat returning -2\n");
        return -2;
    }

}

int capio_lstat_wrapper(const char *path,struct stat *statbuf,long tid) {

    CAPIO_DBG("capio_lstat_wrapper\nlstat  pathanem %s\n", path);

    if (path == NULL)
        return -2;
    std::string absolute_path;
    absolute_path = create_absolute_path(path, capio_dir, current_dir, stat_enabled);
    if (absolute_path.length() == 0)
        return -2;
    return capio_lstat(absolute_path,statbuf,tid);
}


int lstat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result,long my_tid){

    const char *path = reinterpret_cast<const char *>(arg0);
    struct stat *buf = reinterpret_cast<struct stat *>(arg1);
    int res = capio_lstat_wrapper(path, buf, my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}
#endif //CAPIO_LSTAT_HPP
