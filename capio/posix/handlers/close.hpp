#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#if defined(SYS_close)

#include "utils/requests.hpp"
#if defined(SYS_io_uring_setup)
#include "utils/uring.hpp"
#endif

int close_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd   = static_cast<int>(arg0);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%ld)", fd);

#if defined(SYS_io_uring_setup)
    if (uring_close(fd, result)) {
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
#endif

    if (exists_capio_fd(fd)) {
        write_cache->flush();
        read_cache->flush();
        close_request(fd, tid);
        delete_capio_fd(fd);
        *result = 0;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_close
#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP
