#ifndef CAPIO_POSIX_HANDLERS_READ_HPP
#define CAPIO_POSIX_HANDLERS_READ_HPP

#if defined(SYS_read) || defined(SYS_readv)

inline off64_t capio_read(int fd, void *buffer, off64_t count, long tid) {
    START_LOG(tid, "call(fd=%d, buf=0x%08x, count=%ld)", fd, buffer, count);
}

inline ssize_t capio_readv(int fd, const struct iovec *iov, int iovcnt, long tid) {
    START_LOG(tid, "call(fd=%d, iov.iov_base=0x%08x, iov.iov_len=%ld, iovcnt=%d)", fd,
              iov->iov_base, iov->iov_len, iovcnt);

    if (exists_capio_fd(fd)) {
        LOG("fd %d exists and is a capio fd", fd);
        ssize_t tot_bytes = 0;
        ssize_t res       = 0;
        int i             = 0;
        LOG("iov setup completed. starting read request loop");
        while (i < iovcnt && res >= 0) {
            size_t iov_len = iov[i].iov_len;
            if (iov_len != 0) {
                res = capio_read(fd, iov[i].iov_base, iov[i].iov_len, tid);
                if (res == -1) {
                    return CAPIO_POSIX_SYSCALL_ERRNO;
                }
                tot_bytes += res;
            }
            ++i;
        }
        return tot_bytes;
    } else {
        LOG("fd %d is not a capio fd. returning -2", fd);
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }
}

int read_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd       = static_cast<int>(arg0);
    void *buffer = reinterpret_cast<void *>(arg1);
    auto count   = static_cast<off64_t>(arg2);
    long tid     = syscall_no_intercept(SYS_gettid);

    if (exists_capio_fd(fd)) {
        read_request(get_capio_fd_path(fd), get_capio_fd_offset(fd) + count, tid, fd);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

int readv_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd         = static_cast<int>(arg0);
    const auto *iov = reinterpret_cast<const struct iovec *>(arg1);
    auto iovcnt     = static_cast<int>(arg2);
    long tid        = syscall_no_intercept(SYS_gettid);

    // return posix_return_value(capio_readv(fd, iov, iovcnt, tid), result);
    if (exists_capio_fd(fd)) {
        read_request(get_capio_fd_path(fd), get_capio_fd_offset(fd) + iovcnt * sizeof(iovec), tid,
                     fd);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

#endif // SYS_read || SYS_readv
#endif // CAPIO_POSIX_HANDLERS_READ_HPP
