#ifndef CAPIO_FACCESSAT_HPP
#define CAPIO_FACCESSAT_HPP
int capio_faccessat(int dirfd, const char *pathname, int mode, int flags, long tid) {
    int res;
    if (!is_absolute(pathname)) {
        if (dirfd == AT_FDCWD) {
            // pathname is interpreted relative to currdir
            res = capio_access(pathname, mode, tid);
        } else {
            if (is_directory(dirfd) != 1)
                return -2;
            std::string dir_path = get_dir_path(pathname, dirfd);
            if (dir_path.length() == 0)
                return -2;
            std::string path = dir_path + "/" + pathname;
            auto it = std::mismatch(capio_dir->begin(), capio_dir->end(), path.begin());
            if (it.first == capio_dir->end()) {
                if (capio_dir->size() == path.size()) {
                    std::cerr << "ERROR: unlink to the capio_dir " << path << std::endl;
                    exit(1);
                }
                res = capio_file_exists(path, tid);

            } else
                res = -2;
        }
    } else { //dirfd is ignored
        res = capio_file_exists(pathname, tid);
    }
    return res;
}

int faccessat_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long my_tid){

    const char *pathname = reinterpret_cast<const char *>(arg1);
    int dirfd = arg0;

    int res = capio_faccessat(dirfd, pathname, static_cast<int>(arg2),static_cast<int>(arg3), my_tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
       return 0;
    }
    return 1;
}

#endif //CAPIO_FACCESSAT_HPP
