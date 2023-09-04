#ifndef CAPIO_UNLINKAT_HPP
#define CAPIO_UNLINKAT_HPP
int capio_unlinkat(int dirfd,const char *pathname,int flags,long tid) {

    CAPIO_DBG("capio_unlinkat\n");

    if (capio_dir->length() == 0) {
        //unlinkat can be called before initialization (see initialize_from_snapshot)
        return -2;
    }
    int res;
    if (!is_absolute(pathname)) {
        if (dirfd == AT_FDCWD) {
            //pathname is interpreted relative to currdir
            res = capio_unlink(pathname, tid);
        } else {
            if (is_directory(dirfd) != 1)
                return -2;
            std::string dir_path = get_dir_path(pathname, dirfd);
            if (dir_path.length() == 0)
                return -2;
            std::string path = dir_path + "/" + pathname;
            CAPIO_DBG("capio_unlinkat path=%s\n",path.c_str());

            res = capio_unlink_abs(path, tid);
        }
    } else  //dirfd is ignored
        res = capio_unlink_abs(pathname, tid);

    return res;
}

int unlinkat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    const char *pathname = reinterpret_cast<const char *>(arg1);
    int res = capio_unlinkat(static_cast<int>(arg0), pathname, static_cast<int>(arg2), my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_UNLINKAT_HPP
