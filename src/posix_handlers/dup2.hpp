#ifndef CAPIO_DUP2_HPP
#define CAPIO_DUP2_HPP

int capio_dup2(int fd, int fd2, long tid) {
    int res;
    auto it = files->find(fd);

    CAPIO_DBG("capio_dup 2\n");

    if (it != files->end()) {

        CAPIO_DBG("handling capio_dup2\n");

        dup2_enabled = false;
        res = dup2(fd, fd2);
        dup2_enabled = true;
        if (res == -1)
            return -1;
        add_dup_request(fd, res, tid);
        (*files)[res] = (*files)[fd];
        (*capio_files_descriptors)[res] = (*capio_files_descriptors)[fd];

        CAPIO_DBG("handling capio_dup returning res %d\n", res);

    } else
        res = -2;
    return res;
}

int dup2_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    int res;
    if (dup2_enabled) {
        res = capio_dup2(static_cast<int>(arg0), static_cast<int>(arg1), my_tid);
        if (res != -2) {
            *result = (res < 0 ? -errno : res);
            return 0;
        }
    } else
        res = -2;

    return 1;
}


#endif //CAPIO_DUP2_HPP
