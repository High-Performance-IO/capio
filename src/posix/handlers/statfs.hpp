#ifndef CAPIO_POSIX_HANDLERS_STATFS_HPP
#define CAPIO_POSIX_HANDLERS_STATFS_HPP

int fstatfs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    auto fd   = static_cast<int>(arg0);
    auto *buf = reinterpret_cast<struct statfs *>(arg1);
    long tid  = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(fd=%d, buf=0x%08x)", fd, buf);

    if (exists_capio_fd(fd)) {
        std::string path             = get_capio_fd_path(fd);
        const std::string *capio_dir = get_capio_dir();

        *result = static_cast<int>(syscall_no_intercept(SYS_statfs, capio_dir->c_str(), buf));
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_STATFS_HPP
