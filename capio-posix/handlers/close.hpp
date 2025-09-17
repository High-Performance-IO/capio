#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#if defined(SYS_close)

#include "utils/requests.hpp"

inline int close_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result,
                  const pid_t tid) {
    const int fd = static_cast<int>(arg0);
    START_LOG(tid, "call(fd=%ld)", fd);

    if (exists_capio_fd(fd)) {
        close_request(get_capio_fd_path(fd), tid);
        write_request_cache_mem->flush();
        delete_capio_fd(fd);
    }

    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_close
#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP