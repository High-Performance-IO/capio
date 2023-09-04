#ifndef CAPIO_WRITEV_HPP
#define CAPIO_WRITEV_HPP

ssize_t capio_writev(int fd,const struct iovec *iov,int iovcnt,long tid) {
    auto it = files->find(fd);
    if (it != files->end()) {
        ssize_t tot_bytes = 0;
        ssize_t res = 0;
        int i = 0;
        while (i < iovcnt && res >= 0) {
            size_t iov_len = iov[i].iov_len;
            if (iov_len != 0) {
                res = capio_write(fd, iov[i].iov_base, iov[i].iov_len, tid);
                tot_bytes += res;
            }
            ++i;
        }
        if (res == -1)
            return -1;
        else
            return tot_bytes;
    } else
        return -2;

}

int writev_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    int fd = static_cast<int>(arg0);
    const struct iovec *iov = reinterpret_cast<const struct iovec *>(arg1);
    int iovcnt = static_cast<int>(arg2);
    int res = capio_writev(fd, iov, iovcnt, my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}
#endif //CAPIO_WRITEV_HPP
