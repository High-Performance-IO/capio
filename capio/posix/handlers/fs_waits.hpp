#ifndef CAPIO_POSIX_HANDLERS_FS_WAITS_HPP
#define CAPIO_POSIX_HANDLERS_FS_WAITS_HPP

#if !CAPIO_MEM_STREAM

/*
 * Handlers that only the FS variant has: they wait (see utils/fs_waiting.hpp) and give the call
 * to the kernel.
 */

#include <sys/mman.h>

#include "utils/fs_waiting.hpp"

#ifdef SYS_pread64
int pread_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const int fd = static_cast<int>(arg0);
    if (arg3 >= 0 && exists_capio_fd(fd)) {
        fs_wait_for_data(fd, fs_end_of(arg3, arg2), syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

#if defined(SYS_preadv) || defined(SYS_preadv2)
int preadv_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const int fd = static_cast<int>(arg0);
    // preadv2 takes -1 for the file offset
    if (arg3 >= -1 && exists_capio_fd(fd)) {
        const off64_t position = arg3 == -1 ? fs_read_position(fd, nullptr) : arg3;
        const auto *iov        = reinterpret_cast<const struct iovec *>(arg1);
        fs_wait_for_data(fd, fs_end_of(position, fs_total_length(iov, static_cast<int>(arg2))),
                         syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

/// @brief copy_file_range, sendfile and splice: they may return less, but 0 only at the commit
inline int fs_wait_to_copy(const int fd_in, const long offset_pointer, const long count) {
    if (count != 0 && exists_capio_fd(fd_in)) {
        const auto position =
            fs_read_position(fd_in, reinterpret_cast<const off64_t *>(offset_pointer));
        fs_wait_for_data(fd_in, fs_end_of(position, 1), syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int copy_file_range_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                               long *result) {
    return fs_wait_to_copy(static_cast<int>(arg0), arg1, arg4);
}

int sendfile_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                        long *result) {
    return fs_wait_to_copy(static_cast<int>(arg1), arg2, arg3); // (out, in, offset, count)
}

int splice_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    return fs_wait_to_copy(static_cast<int>(arg0), arg1, arg4);
}

#ifdef SYS_mmap
int mmap_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const int fd = static_cast<int>(arg4);
    if (exists_capio_fd(fd)) {
        fs_wait_for_data(fd, FS_UNTIL_COMMIT, syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

#endif // !CAPIO_MEM_STREAM
#endif // CAPIO_POSIX_HANDLERS_FS_WAITS_HPP
