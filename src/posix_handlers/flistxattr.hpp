#ifndef CAPIO_FLISTXATTR_HPP
#define CAPIO_FLISTXATTR_HPP

ssize_t capio_flistxattr(int fd, char *list, ssize_t size) {
    errno = ENOTSUP;
    return -1;
}

int flistxattr_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long my_tid){

    char *list = reinterpret_cast<char *>(arg1);

    int res = capio_flistxattr(static_cast<int>(arg0), list, static_cast<size_t>(arg2));

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_FLISTXATTR_HPP
