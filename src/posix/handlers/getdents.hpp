#ifndef CAPIO_POSIX_HANDLERS_GETDENTS_HPP
#define CAPIO_POSIX_HANDLERS_GETDENTS_HPP

#include "utils/data.hpp"

inline off64_t round(off64_t bytes, bool is_getdents64) {
    off64_t res = 0;
    off64_t ld_size;
    if (is_getdents64) {
        ld_size = THEORETICAL_SIZE_DIRENT64;
    } else {
        ld_size = THEORETICAL_SIZE_DIRENT;
    }
    while (res + ld_size <= bytes) {
        res += ld_size;
    }
    return res;
}

inline off64_t add_getdents_request(int fd, off64_t count,
                                    std::tuple<off64_t *, off64_t *, int, int> &t,
                                    bool is_getdents64, long tid) {
    off64_t offset_upperbound =
        is_getdents64 ? getdents64_request(fd, count, tid) : getdents_request(fd, count, tid);
    off64_t end_of_read = *std::get<0>(t) + count;
    if (end_of_read > offset_upperbound) {
        end_of_read = offset_upperbound;
    }
    return offset_upperbound;
}

// TODO: too similar to capio_read, refactoring needed
inline ssize_t capio_getdents(int fd, void *buffer, size_t count, bool is_getdents64, long tid) {
    START_LOG(tid, "call(fd=%d, dirp=0x%08x, count=%ld, is64bit=%s", fd, buffer, count,
              is_getdents64 ? "true" : "false");

    if (files->find(fd) != files->end()) {
        if (count >= SSIZE_MAX) {
            ERR_EXIT("src does not support read bigger than SSIZE_MAX yet");
        }
        off64_t count_off                             = count;
        std::tuple<off64_t *, off64_t *, int, int> *t = &(*files)[fd];
        off64_t *offset                               = std::get<0>(*t);

        off64_t end_of_read = add_getdents_request(fd, count_off, *t, is_getdents64, tid);
        off64_t bytes_read  = end_of_read - *offset;

        if (bytes_read > count_off) {
            bytes_read = count_off;
        }
        bytes_read = round(bytes_read, is_getdents64);
        read_data(tid, buffer, bytes_read);

        *offset = *offset + bytes_read;

        return bytes_read;
    } else {
        return -2;
    }
}

inline int getdents_handler_impl(long arg0, long arg1, long arg2, long *result, bool is64bit) {
    auto fd    = static_cast<int>(arg0);
    auto *dirp = reinterpret_cast<struct linux_dirent *>(arg1);
    auto count = static_cast<size_t>(arg2);
    long tid   = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, dirp=0x%08x, count=%ld, is64bit=%s", fd, dirp, count,
              is64bit ? "true" : "false");

    auto res = capio_getdents(fd, dirp, count, is64bit, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
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