#ifndef CAPIO_POSIX_HANDLERS_SPLICE_HPP
#define CAPIO_POSIX_HANDLERS_SPLICE_HPP

/**
 * splice and sendfile move data inside the kernel, so CAPIO never sees it: a CAPIO descriptor is a
 * descriptor of /dev/null, and the data would be lost. They fail with EINVAL, as for a file that
 * does not support them, and copy tools such as cp and cat then copy with read and write, which
 * CAPIO handles. copy_file_range, which copies between two files, is done by CAPIO instead: see
 * handlers/copy_file_range.hpp.
 */

#if defined(SYS_splice) || defined(SYS_sendfile)

#include "utils/filesystem.hpp"

inline int capio_refuse_in_kernel_copy(int fd_in, int fd_out, long *result) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd_in=%d, fd_out=%d)", fd_in, fd_out);

    if (exists_capio_fd(fd_in) || exists_capio_fd(fd_out)) {
        *result = -EINVAL;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#ifdef SYS_splice
int splice_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    return capio_refuse_in_kernel_copy(static_cast<int>(arg0), static_cast<int>(arg2), result);
}
#endif

#ifdef SYS_sendfile
int sendfile_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    // sendfile(out_fd, in_fd, offset, count)
    return capio_refuse_in_kernel_copy(static_cast<int>(arg1), static_cast<int>(arg0), result);
}
#endif

#endif // SYS_splice || SYS_sendfile
#endif // CAPIO_POSIX_HANDLERS_SPLICE_HPP
