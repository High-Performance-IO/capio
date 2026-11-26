#ifndef CAPIO_POSIX_HANDLERS_IOCTL_HPP
#define CAPIO_POSIX_HANDLERS_IOCTL_HPP

#if defined(SYS_ioctl)

#include <linux/fs.h>

#include "handlers/read.hpp"

int ioctl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    [[maybe_unused]] const auto fd = static_cast<int>(arg0); // memory variant, and the log
    [[maybe_unused]] const auto request =
        static_cast<unsigned long>(arg1); // FS variant, and the log
    [[maybe_unused]] const long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, request=%lu)", fd, request);

    FS_MODE({
        // A real descriptor: the kernel runs it. FICLONE(dest, src) is an instant snapshot of src,
        // so it waits for its commit.
        int source = -1;
        if (request == FICLONE) {
            source = static_cast<int>(arg2);
        } else if (request == FICLONERANGE && arg2 != 0) {
            source =
                static_cast<int>(reinterpret_cast<const struct file_clone_range *>(arg2)->src_fd);
        }
        if (source >= 0) {
            capio_wait_for_data_fs(source, 0, CAPIO_UNTIL_COMMIT_FS, tid);
        }
        return CAPIO_POSIX_SYSCALL_SKIP;
    })

    MEMORY_MODE({
        // TODO: support ioctl on CAPIO files properly (e.g. FIONREAD, FICLONE) instead of ENOTTY
        if (fd_table->contains(fd)) {
            errno   = ENOTTY;
            *result = -errno;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    })
}

#endif // SYS_ioctl
#endif // CAPIO_POSIX_HANDLERS_IOCTL_HPP
