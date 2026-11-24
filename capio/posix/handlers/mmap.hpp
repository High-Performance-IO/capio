#ifndef CAPIO_POSIX_HANDLERS_MMAP_HPP
#define CAPIO_POSIX_HANDLERS_MMAP_HPP

#if defined(SYS_mmap)

#include "handlers/read.hpp"

int mmap_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    MEMORY_MODE({ return CAPIO_POSIX_SYSCALL_SKIP; })
    FS_MODE({
        // mmap(addr, length, prot, flags, fd, offset). Page faults cannot stream: wait for the
        // commit.
        capio_wait_for_data_fs(static_cast<int>(arg4), 0, CAPIO_UNTIL_COMMIT_FS,
                               syscall_no_intercept(SYS_gettid));
        return CAPIO_POSIX_SYSCALL_SKIP;
    })
}

#endif // SYS_mmap
#endif // CAPIO_POSIX_HANDLERS_MMAP_HPP
