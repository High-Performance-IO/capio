#ifndef CAPIO_POSIX_HANDLERS_GETDENTS_HPP
#define CAPIO_POSIX_HANDLERS_GETDENTS_HPP

#if defined(SYS_getdents) || defined(SYS_getdents64)

#include "capio/data_structure.hpp"

#include "utils/data.hpp"

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
        off64_t end_of_read = getdents_request(fd, count_off, is64bit, tid);
        off64_t bytes_read  = end_of_read - offset;

        if (bytes_read > count_off) {
            bytes_read = count_off;
        }

        read_data(tid, buffer, bytes_read);
        set_capio_fd_offset(fd, offset + bytes_read);

        *result = bytes_read;

        DBG(tid, [](char *result, off64_t count, off64_t offset) {
            struct linux_dirent {
                uint64_t d_ino;
                off64_t d_off;
                unsigned short d_reclen;
                unsigned char d_type;
                char d_name[PATH_MAX];
            };
            START_LOG(syscall_no_intercept(SYS_gettid), "call ()");
            struct linux_dirent *d;
            LOG("READ from queue: offset:%ld, count:%ld", offset, count);
            LOG("%19s %12s %13s %15s %s", "INODE", "TYPE", "RECORD_LENGTH", "OFFSET", "NAME");
            for (size_t bpos = 0, i = 0; bpos < count && i < 10; i++) {
                d = (struct linux_dirent *) (result + bpos);
                LOG("%19lu %9s %13ld %15ld %s\n", d->d_ino,
                    (d->d_type == 8)    ? "regular"
                    : (d->d_type == 4)  ? "directory"
                    : (d->d_type == 1)  ? "FIFO"
                    : (d->d_type == 12) ? "socket"
                    : (d->d_type == 10) ? "symlink"
                    : (d->d_type == 6)  ? "block dev"
                    : (d->d_type == 2)  ? "char dev"
                                        : "???",
                    d->d_reclen, d->d_off, d->d_name);
                bpos += d->d_reclen;
            }
        }((char *) buffer, bytes_read, offset));

        return CAPIO_POSIX_SYSCALL_SUCCESS;
    } else {
        LOG("fd=%d, is not a capio file descriptor", fd);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

inline int getdents_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                            long *result) {
    return getdents_handler_impl(arg0, arg1, arg2, result, false);
}

inline int getdents64_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                              long *result) {
    return getdents_handler_impl(arg0, arg1, arg2, result, true);
}

#endif // SYS_getdents || SYS_getdents64
#endif // CAPIO_POSIX_HANDLERS_GETDENTS_HPP
