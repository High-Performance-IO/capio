#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#if defined(SYS_close)

#include "utils/requests.hpp"

int close_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd   = static_cast<int>(arg0);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%ld)", fd);

    if (!fd_table->contains(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    MEMORY_MODE({
        write_cache->flush();
        read_cache->flush();
        close_request(fd, tid);
        fd_table->remove(fd);
        *result = 0;
        return CAPIO_POSIX_SYSCALL_SKIP;
    })
    FS_MODE({
        // The kernel closes first, then CAPIO announces the close. On NFS, close(2) is when the
        // data reaches the server and when a full disk is reported: the file must not be committed
        // before.
        const std::string path = fd_table->path(fd);
        fd_table->remove(fd); // before close: another thread may get the same number right after
        *result = syscall_no_intercept(SYS_close, fd);
        // The server does not count a close that failed. EINTR: Linux has closed it anyway
        close_request_fs(path, *result < 0 && *result != -EINTR ? -*result : 0, tid);
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    })
}

#endif // SYS_close
#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP
