#ifndef CAPIO_POSIX_HANDLERS_SPLICE_HPP
#define CAPIO_POSIX_HANDLERS_SPLICE_HPP

/**
 * splice and sendfile move data inside the kernel, so CAPIO never sees it. In the memory variant a
 * CAPIO descriptor is a descriptor of /dev/null, and the data would be lost. They fail with
 * EINVAL, as for a file that does not support them, and copy tools such as cp and cat then copy
 * with read and write, which CAPIO handles. copy_file_range, which copies between two files, is
 * done by CAPIO instead: see handlers/copy_file_range.hpp.
 *
 * In the FS variant the kernel copies, into or out of a CAPIO file, once there is data to copy.
 */

#if defined(SYS_splice) || defined(SYS_sendfile)

#include "handlers/read.hpp"
#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/tid.hpp"

/// @param off_in the offset of the copy in @p fd_in, or nullptr for the file offset
inline int capio_in_kernel_copy(int fd_in, const loff_t *off_in, size_t count, int fd_out,
                                long *result) {
    START_LOG(capio_tid(), "call(fd_in=%d, fd_out=%d)", fd_in, fd_out);

    FS_MODE({
        // It may copy less, but 0 only at the commit
        if (count != 0) {
            capio_wait_for_data_fs(fd_in, off_in != nullptr ? *off_in : -1, 1, capio_tid());
        }
        return CAPIO_POSIX_SYSCALL_SKIP;
    })

    MEMORY_MODE({
        if (fd_table->contains(fd_in) || fd_table->contains(fd_out)) {
            *result = -EINVAL;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        return CAPIO_POSIX_SYSCALL_SKIP;
    })
}

#ifdef SYS_splice
int splice_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    // splice(fd_in, off_in, fd_out, off_out, len, flags)
    const auto fd_in   = static_cast<int>(arg0);
    const auto *off_in = reinterpret_cast<const loff_t *>(arg1);
    const auto fd_out  = static_cast<int>(arg2);
    const auto len     = static_cast<size_t>(arg4);
    START_LOG(capio_tid(), "call(fd_in=%d, fd_out=%d, len=%zu)", fd_in, fd_out, len);

    return capio_in_kernel_copy(fd_in, off_in, len, fd_out, result);
}
#endif

#ifdef SYS_sendfile
int sendfile_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    // sendfile(out_fd, in_fd, offset, count)
    const auto out_fd  = static_cast<int>(arg0);
    const auto in_fd   = static_cast<int>(arg1);
    const auto *offset = reinterpret_cast<const loff_t *>(arg2);
    const auto count   = static_cast<size_t>(arg3);
    START_LOG(capio_tid(), "call(out_fd=%d, in_fd=%d, count=%zu)", out_fd, in_fd, count);

    return capio_in_kernel_copy(in_fd, offset, count, out_fd, result);
}
#endif

#endif // SYS_splice || SYS_sendfile
#endif // CAPIO_POSIX_HANDLERS_SPLICE_HPP
