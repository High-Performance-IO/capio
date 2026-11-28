#ifndef CAPIO_POSIX_HANDLERS_GETDENTS_HPP
#define CAPIO_POSIX_HANDLERS_GETDENTS_HPP

#if defined(SYS_getdents) || defined(SYS_getdents64)

#include <cstring>

#include "common/constants.hpp"
#include "common/dirent.hpp"

#include "utils/common.hpp"
#include "utils/data.hpp"
#include "utils/tid.hpp"

/// getdents reads a CAPIO directory as read reads a file, through capio_cached_read
inline int getdents_handler_impl(long arg0, long arg1, long arg2, long *result, bool is64bit) {
    auto fd      = static_cast<int>(arg0);
    auto *buffer = reinterpret_cast<struct linux_dirent64 *>(arg1);
    auto count   = static_cast<off64_t>(arg2);
    START_LOG(capio_tid(), "call(fd=%d, dirp=0x%08x, count=%ld, is64bit=%s)", fd, buffer, count,
              is64bit ? "true" : "false");

    if (fd_table->contains(fd)) {
        LOG("fd=%d, is a capio file descriptor", fd);
        // The FS server lets the kernel list the directory, once it may be listed, unless it
        // returns the entries itself: for a consumer of an on_n_files directory in no_update mode
        FS_MODE({
            if (read_request_cache->isListedByKernel(fd)) {
                return CAPIO_POSIX_SYSCALL_SKIP;
            }
        })

        *result = capio_cached_read(fd, buffer, count, true, is64bit);
        FS_MODE({
            if (*result == sizeof(linux_dirent64) &&
                memcmp(buffer, KERNEL_LISTS_MARKER.data(), KERNEL_LISTS_MARKER.size()) == 0) {
                fd_table->setOffset(fd, fd_table->offset(fd) - *result);
                read_request_cache->setListedByKernel(fd);
                return CAPIO_POSIX_SYSCALL_SKIP;
            }
        })

        DBG(capio_tid(), [](char *buf, off64_t count) {
            START_LOG(capio_tid(), "call (count=%ld)", count);
            struct linux_dirent64 *d;
            LOG("%25s %12s %13s %15s   %s", "INODE", "TYPE", "RECORD_LENGTH", "OFFSET", "NAME");
            for (off64_t bpos = 0, i = 0; bpos < count && i < 10; i++) {
                d = (struct linux_dirent64 *) (buf + bpos);
                LOG("%25lu %12s %13ld %15ld   %s\n", d->d_ino,
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
        }(reinterpret_cast<char *>(buffer), *result));

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
