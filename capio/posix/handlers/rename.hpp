#ifndef CAPIO_POSIX_HANDLERS_RENAME_HPP
#define CAPIO_POSIX_HANDLERS_RENAME_HPP

#include "utils/filesystem.hpp"

int renameat2_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    const auto flags                            = arg4;
    const std::filesystem::path old_dir_fd_path = get_dir_path(static_cast<int>(arg0));
    const std::filesystem::path old_path(reinterpret_cast<const char *>(arg1));
    const std::filesystem::path new_dir_fd_path = get_dir_path(static_cast<int>(arg2));
    const std::filesystem::path new_path(reinterpret_cast<const char *>(arg3));
    const long tid = syscall_no_intercept(SYS_gettid);

    // TODO: implement handling of FLAGS

    START_LOG(tid,
              "call(old_dir_fd_path=%s, old_path=%s, new_dir_fd_path=%s, newpath=%s, flags=%d)",
              old_dir_fd_path.c_str(), old_path.c_str(), new_dir_fd_path.c_str(), new_path.c_str(),
              flags);

    // Resolve paths relative to path of dir_fd ONLY if input path is not absolute
    const auto old_path_abs =
        old_path.is_absolute() ? old_path : capio_absolute(old_dir_fd_path / old_path);

    const auto new_path_abs =
        new_path.is_absolute() ? new_path : capio_absolute(new_dir_fd_path / new_path);

    if (is_prefix(old_path_abs, new_path_abs)) { // TODO: The check is more complex
        errno   = EINVAL;
        *result = -errno;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }

    if (is_capio_path(old_path_abs)) {
        rename_capio_path(old_path_abs, new_path_abs);
        auto res = rename_request(tid, old_path_abs, new_path_abs);
        *result  = (res < 0 ? -errno : res);
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    } else {
        if (is_capio_path(new_path_abs)) {
            std::filesystem::copy(old_path_abs, new_path_abs);
            *result = -errno;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        } else {
            return CAPIO_POSIX_SYSCALL_SKIP;
        }
    }
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
