#ifndef CAPIO_MKLDIRAT_HPP
#define CAPIO_MKLDIRAT_HPP

int capio_mkdirat(int dirfd,const char *pathname,mode_t mode,long tid) {

    CAPIO_DBG("capio_mkdirat %s\n", pathname);

    std::string path_to_check;
    if (is_absolute(pathname)) {
        path_to_check = pathname;

        CAPIO_DBG("capio_mkdirat absolute %s\n", path_to_check.c_str());

    } else {
        if (dirfd == AT_FDCWD) {
            path_to_check = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
            if (path_to_check.length() == 0)
                return -2;

            CAPIO_DBG("capio_mkdirat AT_FDCWD %s\n", path_to_check.c_str());

        } else {

            if (is_directory(dirfd) != 1)
                return -2;
            std::string dir_path = get_dir_path(pathname, dirfd);
            if (dir_path.length() == 0)
                return -2;
            path_to_check = dir_path + "/" + pathname;
            CAPIO_DBG("capio_mkdirat with dirfd %s\n", path_to_check.c_str());
        }
    }
    return request_mkdir(path_to_check, tid);
}


int mkdirat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    const char *pathname = reinterpret_cast<const char *>(arg1);
    int dirfd = arg0;
    int res = capio_mkdirat(dirfd,pathname,static_cast<mode_t>(arg2),my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}
#endif //CAPIO_MKLDIRAT_HPP
