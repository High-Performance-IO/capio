#ifndef CAPIO_POSIX_HANDLERS_READ_HPP
#define CAPIO_POSIX_HANDLERS_READ_HPP

#include "utils/data.hpp"

int read_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd       = static_cast<int>(arg0);
    void *buffer = reinterpret_cast<void *>(arg1);
    auto count   = static_cast<off64_t>(arg2);
    long tid     = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, buf=0x%08x, count=%ld)", fd, buffer, count);

    if (exists_capio_fd(fd)) {
        if (count >= SSIZE_MAX) {
            ERR_EXIT("src does not support read bigger than SSIZE_MAX yet");
        }
        off64_t count_off   = count;
        off64_t offset      = get_capio_fd_offset(fd);
        off64_t end_of_read = read_request(fd, count_off, tid);
        off64_t bytes_read  = end_of_read - offset;
        read_data(tid, buffer, bytes_read);
        set_capio_fd_offset(fd, offset + bytes_read);
        *result = bytes_read;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // CAPIO_POSIX_HANDLERS_READ_HPP
