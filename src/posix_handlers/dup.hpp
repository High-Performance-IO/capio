#ifndef CAPIO_DUP_HPP
#define CAPIO_DUP_HPP

int capio_dup(int fd, long tid) {
    int res;
    auto it = files->find(fd);

    CAPIO_DBG("capio_dup\n");

    if (it != files->end()) {

        CAPIO_DBG("handling capio_dup\n");

        res = open("/dev/null", O_WRONLY);
        if (res == -1)
            err_exit("open in capio_dup", "capio_dup");
        add_dup_request(fd, res, tid);
        (*files)[res] = (*files)[fd];
        (*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];

        CAPIO_DBG("handling capio_dup returning res %d\n", res);

    } else
        res = -2;
    return res;
}

int dup_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    int res = capio_dup(static_cast<int>(arg0), my_tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_DUP_HPP
