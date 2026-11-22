#ifndef CAPIO_POSIX_HANDLERS_IOCTL_HPP
#define CAPIO_POSIX_HANDLERS_IOCTL_HPP

#if defined(SYS_ioctl)

#include <linux/fs.h>

#include "utils/fs_waiting.hpp"

/// @brief Wait for the commit of the source of a FICLONE or FICLONERANGE
inline void fs_wait_before_clone(const unsigned long request, const long argument, const long tid) {
    int source = -1;
    if (request == FICLONE) {
        source = static_cast<int>(argument);
    } else if (request == FICLONERANGE && argument != 0) {
        source =
            static_cast<int>(reinterpret_cast<const struct file_clone_range *>(argument)->src_fd);
    }
    if (source >= 0 && exists_capio_fd(source)) {
        fs_wait_for_data(source, FS_UNTIL_COMMIT, tid);
    }
}

int ioctl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const auto fd  = static_cast<int>(arg0);
    const long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, request=%ld)", fd, arg1);

    // FICLONE(dest, src) is an instant snapshot of src. dest may be outside CAPIO_DIR.
    CAPIO_STORAGE_CALL(, fs_wait_before_clone(static_cast<unsigned long>(arg1), arg2, tid));

    if (exists_capio_fd(fd)) {
        CAPIO_STORAGE_CALL(
            {
                errno   = ENOTTY;
                *result = -errno;
                return CAPIO_POSIX_SYSCALL_SUCCESS;
            },
            {});
    }
    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

#endif // SYS_ioctl
#endif // CAPIO_POSIX_HANDLERS_IOCTL_HPP
