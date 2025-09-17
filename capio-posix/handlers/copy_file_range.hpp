#ifndef CAPIO_COPY_FILE_RANGE_HPP
#define CAPIO_COPY_FILE_RANGE_HPP

inline int copy_file_range_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                                   long *result, const pid_t tid) {

    auto fd_in  = static_cast<int>(arg0);
    auto off_in = static_cast<capio_off64_t>(arg1);

    // auto fd_out  = static_cast<int>(arg2);
    // auto off_out = static_cast<capio_off64_t>(arg3);

    START_LOG(tid, "call()");

    // TODO: support in memory read  / write
    if (exists_capio_fd(fd_in)) {
        auto path = get_capio_fd_path(fd_in);
        LOG("Handling copy for source file: %s", path.c_str());
        read_request_cache_fs->read_request(path, off_in, tid, fd_in);
    }

    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif // CAPIO_COPY_FILE_RANGE_HPP