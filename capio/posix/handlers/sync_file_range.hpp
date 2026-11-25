#ifndef CAPIO_POSIX_HANDLERS_SYNC_FILE_RANGE_HPP
#define CAPIO_POSIX_HANDLERS_SYNC_FILE_RANGE_HPP

#if defined(SYS_sync_file_range) || defined(SYS_sync_file_range2)

#include "utils/common.hpp"
#include "utils/filesystem.hpp"

/**
 * sync_file_range and sync_file_range2, which only differ in the order of their arguments after the
 * descriptor. The memory variant does not implement them: its descriptors are of /dev/null, and the
 * kernel would sync nothing. The FS variant lets the kernel run them, as its descriptors are real.
 */
int sync_file_range_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                            long *result) {
    [[maybe_unused]] const auto fd  = static_cast<int>(arg0); // memory variant, and the log
    [[maybe_unused]] const long tid = syscall_no_intercept(SYS_gettid); // the log
    START_LOG(tid, "call(fd=%d)", fd);

    MEMORY_MODE({
        if (fd_table->contains(fd)) {
            throw CapioUnimplementedException("sync_file_range");
        }
    })
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_sync_file_range || SYS_sync_file_range2
#endif // CAPIO_POSIX_HANDLERS_SYNC_FILE_RANGE_HPP
