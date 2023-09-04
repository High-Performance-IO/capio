#ifndef CAPIO_CREAT_HPP
#define CAPIO_CREAT_HPP

int creat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    const char *pathname = reinterpret_cast<const char *>(arg0);

    CAPIO_DBG("capio_creat %s\n", pathname);

    int res = capio_openat(AT_FDCWD,pathname,O_CREAT | O_WRONLY | O_TRUNC,true,my_tid);

    CAPIO_DBG("capio_creat %s returning %d\n", pathname, res);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}


#endif //CAPIO_CREAT_HPP
