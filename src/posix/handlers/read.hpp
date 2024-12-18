#ifndef CAPIO_POSIX_HANDLERS_READ_HPP
#define CAPIO_POSIX_HANDLERS_READ_HPP

#if defined(SYS_read) || defined(SYS_readv)

int read_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd      = static_cast<int>(arg0);
    auto buffer = reinterpret_cast<char *>(arg1);
    auto count  = static_cast<capio_off64_t>(arg2);
    auto tid    = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, tid=%d, count=%ld)", fd, tid, count);

    if (exists_capio_fd(fd)) {
        auto file_path = get_capio_fd_path(fd);

        if (store_file_in_memory(file_path, capio_syscall(SYS_gettid))) {
            read_request_cache_mem->read(fd, buffer, count);
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }

        auto computed_offset = get_capio_fd_offset(fd) + count;

        LOG("Handling read on file %s up to byte %ld", get_capio_fd_path(fd).c_str(),
            computed_offset);

        read_request_cache_fs->read_request(get_capio_fd_path(fd), computed_offset, tid, fd);

        set_capio_fd_offset(fd, computed_offset);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

int readv_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd     = static_cast<int>(arg0);
    auto buffer = reinterpret_cast<char *>(arg1);
    auto iovcnt = static_cast<int>(arg2);
    auto tid    = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    if (exists_capio_fd(fd)) {
        auto file_path = get_capio_fd_path(fd);
        if (store_file_in_memory(file_path, capio_syscall(SYS_gettid))) {
            read_request_cache_mem->read(fd, buffer, iovcnt);
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }

        auto computed_offset = get_capio_fd_offset(fd) + iovcnt * sizeof(iovec);

        read_request_cache_fs->read_request(get_capio_fd_path(fd), computed_offset, tid, fd);

        set_capio_fd_offset(fd, computed_offset);
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

#endif // SYS_read || SYS_readv
#endif // CAPIO_POSIX_HANDLERS_READ_HPP
