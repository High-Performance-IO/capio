#ifndef CAPIO_FSTATAT_HPP
#define CAPIO_FSTATAT_HPP
int capio_fstatat(int dirfd,const char *pathname,struct stat *statbuf,int flags,long tid) {

    CAPIO_DBG("fstatat pathanem %s\n", pathname);

    if ((flags & AT_EMPTY_PATH) == AT_EMPTY_PATH) {
        if (dirfd == AT_FDCWD) { // operate on currdir
            char *curr_dir = get_current_dir_name();
            std::string path(curr_dir);
            free(curr_dir);
            return capio_lstat(path, statbuf, tid);
        } else { // operate on dirfd. in this case dirfd can refer to any type of file
            if (strlen(pathname) == 0)
                return capio_fstat(dirfd, statbuf, tid);
            else {
                //TODO: set errno
                return -1;
            }
        }
    }

    int res = -1;

    if (!is_absolute(pathname)) {
        if (dirfd == AT_FDCWD) {
            // pathname is interpreted relative to currdir
            res = capio_lstat_wrapper(pathname, statbuf, tid);
        } else {
            if (is_directory(dirfd) != 1)
                return -2;
            auto it = capio_files_descriptors->find(dirfd);
            std::string dir_path;
            if (it == capio_files_descriptors->end())
                dir_path = get_dir_path(pathname, dirfd);
            else
                dir_path = it->second;
            if (dir_path.length() == 0)
                return -2;
            std::string pathstr = pathname;
            if (pathstr.substr(0, 2) == "./") {
                pathstr = pathstr.substr(2, pathstr.length() - 1);
                pathname = pathstr.c_str();

                CAPIO_DBG("path modified %s\n", pathname);
            }
            std::string path;
            if (pathname[strlen(pathname) - 1] == '.')
                path = dir_path;
            else
                path = dir_path + "/" + pathname;
            res = capio_lstat(path, statbuf, tid);
        }
    } else { //dirfd is ignored
        res = capio_lstat(std::string(pathname), statbuf, tid);
    }
    return res;

}


int fstatat_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long my_tid){

    const char *pathname = reinterpret_cast<const char *>(arg1);
    struct stat *statbuf = reinterpret_cast<struct stat *>(arg2);

    int res = capio_fstatat(static_cast<int>(arg0),pathname,statbuf,static_cast<int>(arg3),my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_FSTATAT_HPP
