#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#if defined(SYS_close)

#include "utils/requests.hpp"

int close_handler(pid_t tid, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                  long *result) {
    int fd = static_cast<int>(arg0);
    START_LOG(tid, "call(fd=%ld)", fd);

    if (exists_capio_fd(tid, fd)) {
        write_cache->flush(tid);
        close_request(fd, tid);
        delete_capio_fd(fd);
        *result = 0;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_close
#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP
