#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#if defined(SYS_close)

#include "utils/requests.hpp"
#include "utils/tid.hpp"

int close_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd = static_cast<int>(arg0);
    START_LOG(capio_tid(), "call(fd=%ld)", fd);

    if (!fd_table->contains(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    MEMORY_MODE({
        write_cache->flush();
        read_cache->flush();
    })
    // The kernel closes first, then the server is told. FS variant: on NFS, close(2) is when the
    // data reaches the file server and when a full disk is reported, so the file must not be
    // committed before. The server does not count a close that failed. EINTR: Linux has closed it
    // anyway.
    *result = fd_table->close(fd, [fd](const long res) {
        close_request(fd, capio_tid(), res < 0 && res != -EINTR ? -res : 0);
    });
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // SYS_close
#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP
