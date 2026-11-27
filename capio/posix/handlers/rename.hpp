#ifndef CAPIO_POSIX_HANDLERS_RENAME_HPP
#define CAPIO_POSIX_HANDLERS_RENAME_HPP

#include <cstdio> // RENAME_EXCHANGE

#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"
#include "utils/tid.hpp"

int renameat2_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    const std::filesystem::path old_dir_fd_path = get_dir_path(static_cast<int>(arg0));
    const std::filesystem::path old_path(reinterpret_cast<const char *>(arg1));
    const std::filesystem::path new_dir_fd_path = get_dir_path(static_cast<int>(arg2));
    const std::filesystem::path new_path(reinterpret_cast<const char *>(arg3));
    const long tid = capio_tid();

    START_LOG(
        tid, "call(old_dir_fd_path=%s, old_path=%s, new_dir_fd_path=%s, newpath=%s, flags=%d)",
        old_dir_fd_path.c_str(), old_path.c_str(), new_dir_fd_path.c_str(), new_path.c_str(), arg4);

    // Resolve paths relative to path of dir_fd ONLY if input path is not absolute
    const auto old_path_abs =
        old_path.is_absolute() ? old_path : capio_absolute(old_dir_fd_path / old_path);
    const auto new_path_abs =
        new_path.is_absolute() ? new_path : capio_absolute(new_dir_fd_path / new_path);

    const bool old_is_capio = is_capio_path(old_path_abs);
    const bool new_is_capio = is_capio_path(new_path_abs);
    if (!old_is_capio && !new_is_capio) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    FS_MODE({
        // Let the kernel do the rename and its error checks. If it worked, update our descriptors
        // and tell the server, which moves what it knows about the file to the new name.
        // RENAME_EXCHANGE swaps two files without giving either a new name, so the server is not
        // told.
        *result = syscall_no_intercept(SYS_renameat2, arg0, arg1, arg2, arg3, arg4);
        if (*result < 0 || old_path_abs == new_path_abs || (arg4 & RENAME_EXCHANGE) != 0) {
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        if (old_is_capio) {
            // Descriptors follow the file. A file moved out of CAPIO_DIR is no longer ours
            new_is_capio ? fd_table->renamePath(old_path_abs, new_path_abs)
                         : fd_table->removePath(old_path_abs);
        }
        rename_request_fs(old_path_abs, old_is_capio, new_path_abs, new_is_capio, tid);
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    })

    MEMORY_MODE({
        // Only RENAME_NOREPLACE: Linux fails the other flags with EINVAL on the file systems that
        // lack them
        if ((arg4 & ~RENAME_NOREPLACE) != 0) {
            *result = -EINVAL;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        const off64_t old_access = old_is_capio ? access_request(old_path_abs, tid) : 0;
        if (old_access == CAPIO_POSIX_SYSCALL_REQUEST_SKIP) { // excluded from CAPIO
            return CAPIO_POSIX_SYSCALL_SKIP;
        }
        if (old_access != 0) {
            *result = -ENOENT;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        if (old_path_abs == new_path_abs) { // a rename of a file to itself does nothing
            *result = 0;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        if (is_prefix(old_path_abs, new_path_abs)) {
            // Into a directory's own subtree, or under a file, which is not a directory
            const bool old_is_dir = !old_is_capio || stat_request(old_path_abs, tid).second == 1;
            *result               = old_is_dir ? -EINVAL : -ENOTDIR;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        if ((arg4 & RENAME_NOREPLACE) != 0 &&
            (new_is_capio ? access_request(new_path_abs, tid) == 0
                          : syscall_no_intercept(SYS_faccessat, AT_FDCWD, new_path_abs.c_str(),
                                                 F_OK) == 0)) {
            *result = -EEXIST;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }

        if (old_is_capio) {
            // The descriptors stay CAPIO's: the memory server keeps the file for them, even when it
            // leaves CAPIO_DIR
            fd_table->renamePath(old_path_abs, new_path_abs);
            auto res = rename_request(tid, old_path_abs, new_path_abs, new_is_capio);
            *result  = (res < 0 ? -errno : res);
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        std::filesystem::copy(old_path_abs, new_path_abs); // moved into CAPIO_DIR from outside
        *result = -errno;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    })
}

int rename_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    // Force flags to zero to ensure that spurious data is being passed to handler
    return renameat2_handler(AT_FDCWD, arg0, AT_FDCWD, arg1, 0, 0, result);
}

int renameat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    // Force flags to zero to ensure that spurious data is being passed to handler
    return renameat2_handler(arg0, arg1, arg2, arg3, 0, arg5, result);
}

#endif // CAPIO_POSIX_HANDLERS_RENAME_HPP
