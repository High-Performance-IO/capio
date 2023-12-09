#ifndef CAPIO_POSIX_HANDLERS_GETDENTS_HPP
#define CAPIO_POSIX_HANDLERS_GETDENTS_HPP

#include "utils/data.hpp"
#include <dirent.h>

inline off64_t round(off64_t bytes, bool is_getdents64) {
    off64_t res = 0;
    off64_t ld_size;
    ld_size = THEORETICAL_SIZE_DIRENT64;

    while (res + ld_size <= bytes) {
        res += ld_size;
    }
    return res;
}

// TODO: too similar to capio_read, refactoring needed
inline int getdents_handler_impl(long arg0, long arg1, long arg2, long *result, bool is64bit) {
    auto fd      = static_cast<int>(arg0);
    auto *buffer = reinterpret_cast<struct linux_dirent64 *>(arg1);
    auto count   = static_cast<size_t>(arg2);
    long tid     = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(fd=%d, dirp=0x%08x, count=%ld, is64bit=%s)", fd, buffer, count,
              is64bit ? "true" : "false");

    if (exists_capio_fd(fd)) {
        LOG("fd=%d, is a capio file descriptor", fd);

        if (count >= SSIZE_MAX) {
            ERR_EXIT("src does not support read bigger than SSIZE_MAX yet");
        }
        auto count_off      = static_cast<off64_t>(count);
        off64_t offset      = get_capio_fd_offset(fd);
        off64_t end_of_read = add_getdents_request(fd, count_off, is64bit, tid);
        off64_t bytes_read  = end_of_read - offset;

        if (bytes_read > count_off) {
            bytes_read = count_off;
        }

        bytes_read = round(bytes_read, is64bit);
        read_data(tid, buffer, bytes_read);
        set_capio_fd_offset(fd, offset + bytes_read);

        *result = bytes_read;
        return 0;
    } else {
        LOG("fd=%d, is not a capio file descriptor", fd);
    }
    return 1;
}

inline int getdents_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                            long *result) {
    return getdents_handler_impl(arg0, arg1, arg2, result, false);
}

inline int getdents64_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                              long *result) {
    return getdents_handler_impl(arg0, arg1, arg2, result, true);
}

#endif // CAPIO_POSIX_HANDLERS_GETDENTS_HPP
