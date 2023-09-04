#ifndef CAPIO_UNLINK_HPP
#define CAPIO_UNLINK_HPP
int capio_unlink(const char *pathname,long tid) {

    CAPIO_DBG("capio_unlink %s\n", pathname);
    if (capio_dir == nullptr) {
        //unlink can be called before initialization (see initialize_from_snapshot)
        return -2;
    }
    std::string abs_path = create_absolute_path(pathname, capio_dir, current_dir, stat_enabled);
    if (abs_path.length() == 0)
        return -2;

    return capio_unlink_abs(abs_path, tid);
}


int unlink_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result,  long my_tid){


    const char *pathname = reinterpret_cast<const char *>(arg0);
    int res = capio_unlink(pathname, my_tid);


    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}
#endif //CAPIO_UNLINK_HPP
