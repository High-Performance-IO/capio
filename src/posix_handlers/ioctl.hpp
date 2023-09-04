#ifndef CAPIO_IOCTL_HPP
#define CAPIO_IOCTL_HPP
int capio_ioctl(int fd, unsigned long request) {

    CAPIO_DBG("capio ioctl %d %lu\n", fd, request);

    if (files->find(fd) != files->end()) {
        CAPIO_DBG("capio ioctl ENOTTY %d %lu\n", fd, request);
        errno = ENOTTY;
        return -1;
    } else {
        CAPIO_DBG("capio ioctl returning -2\n");
        return -2;
    }

}

int ioctl_handler(long arg0, long arg1, long arg2,  long arg3, long arg4, long arg5, long* result, long my_tid){

    int res = capio_ioctl(static_cast<int>(arg0), static_cast<unsigned long>(arg1));
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_IOCTL_HPP
