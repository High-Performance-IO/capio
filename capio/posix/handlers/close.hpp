#ifndef CAPIO_POSIX_HANDLERS_CLOSE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_HPP

#if defined(SYS_close)

#include "utils/requests.hpp"
#if !CAPIO_MEM_STREAM
#include "utils/fs_descriptors.hpp"
#endif

int close_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd   = static_cast<int>(arg0);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%ld)", fd);

    if (exists_capio_fd(fd)) {
        write_cache->flush();
        read_cache->flush();
        close_request(fd, tid);
        delete_capio_fd(fd);
        *result = 0;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#if !CAPIO_MEM_STREAM
/// @brief FS variant: the kernel closes first, then CAPIO announces the close
int close_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const int fd = static_cast<int>(arg0);
    if (!exists_capio_fd(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    return fs_close(fd, syscall_no_intercept(SYS_gettid), result);
}
#endif // !CAPIO_MEM_STREAM

#endif // SYS_close
#endif // CAPIO_POSIX_HANDLERS_CLOSE_HPP
