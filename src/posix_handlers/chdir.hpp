#ifndef CAPIO_CHDIR_HPP
#define CAPIO_CHDIR_HPP
/*
 * chdir could be done to a CAPIO dir that is not present in the filesystem.
 * For this reason if chdir is done to a CAPIO directory we don't give control
 * to the kernel.
 */

int capio_chdir(const char *path) {

    std::string path_to_check;
    CAPIO_DBG("capio_chdir captured %s\n", path);

    if (is_absolute(path)) {
        path_to_check = path;
        CAPIO_DBG("capio_chdir absolute %s\n", path_to_check.c_str());
    } else {
        path_to_check = create_absolute_path(path, capio_dir, current_dir, stat_enabled);
    }

    auto res = std::mismatch(capio_dir->begin(), capio_dir->end(), path_to_check.begin());

    CAPIO_DBG("CAPIO directory: %s\n", capio_dir->c_str());

    if (res.first == capio_dir->end()) {
        delete current_dir;

        CAPIO_DBG("current dir changed: %s\n", path_to_check.c_str());

        current_dir = new std::string(path_to_check);
        return 0;
    } else {
        return -2;
    }
}

int chdir_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long *result,long my_tid) {

    const char *path = reinterpret_cast<const char *>(arg0);
    int res = capio_chdir(path);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}


#endif //CAPIO_CHDIR_HPP
