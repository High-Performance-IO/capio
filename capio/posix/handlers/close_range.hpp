#ifndef CAPIO_POSIX_HANDLERS_CLOSE_RANGE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_RANGE_HPP

#if defined(SYS_close_range) && !CAPIO_MEM_STREAM

#include "utils/fs_descriptors.hpp"

int close_range_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                        long *result) {
    const long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(first=%ld, last=%ld, flags=%ld)", arg0, arg1, arg2);
    return fs_close_range(static_cast<unsigned>(arg0), static_cast<unsigned>(arg1),
                          static_cast<unsigned>(arg2), tid, result);
}

#endif // SYS_close_range
#endif // CAPIO_POSIX_HANDLERS_CLOSE_RANGE_HPP
