#ifndef CAPIO_ACCESS_HPP
#define CAPIO_ACCESS_HPP

int capio_access(const char *pathname, int mode, long tid) {

    std::string abs_pathname = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
    abs_pathname = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
    if (abs_pathname.length() == 0) {
        errno = ENONET;
        return -1;
    }
    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), abs_pathname.begin());
    if (res.first == capio_dir->end())
        return capio_file_exists(abs_pathname, tid);
    else
        return -2;
}

int access_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long *result,long my_tid) {

    const char *pathname = reinterpret_cast<const char *>(arg0);

    int res = capio_access(pathname, static_cast<int>(arg1), my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_ACCESS_HPP
