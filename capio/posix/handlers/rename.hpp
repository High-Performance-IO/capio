#ifndef CAPIO_POSIX_HANDLERS_RENAME_HPP
#define CAPIO_POSIX_HANDLERS_RENAME_HPP

#include "utils/filesystem.hpp"

#if defined(SYS_rename)

int rename_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const std::filesystem::path oldpath(reinterpret_cast<const char *>(arg0));
    const std::filesystem::path newpath(reinterpret_cast<const char *>(arg1));
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(oldpath=%s, newpath=%s)", oldpath.c_str(), newpath.c_str());

    auto oldpath_abs = capio_absolute(oldpath);
    auto newpath_abs = capio_absolute(newpath);

    if (is_prefix(oldpath_abs, newpath_abs)) { // TODO: The check is more complex
        errno   = EINVAL;
        *result = -errno;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }

    if (is_capio_path(oldpath_abs)) {
        rename_capio_path(oldpath_abs, newpath_abs);
        auto res = rename_request(tid, oldpath_abs, newpath_abs);
        *result  = (res < 0 ? -errno : res);
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    } else {
        if (is_capio_path(newpath_abs)) {
            std::filesystem::copy(oldpath_abs, newpath_abs);
            *result = -errno;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        } else {
            return CAPIO_POSIX_SYSCALL_SKIP;
        }
    }
}

#endif // SYS_rename

#if defined(SYS_renameat)

int renameat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const auto old_dirfd = arg0, new_dirfd = arg2;
    const std::filesystem::path oldpath(reinterpret_cast<const char *>(arg1));
    const std::filesystem::path newpath(reinterpret_cast<const char *>(arg3));
    long tid = syscall_no_intercept(SYS_gettid);

    START_LOG(tid, "call(oldpath=%s, newpath=%s)", oldpath.c_str(), newpath.c_str());

    if (old_dirfd != AT_FDCWD || new_dirfd != AT_FDCWD) {
        LOG("Rename with dirfd different from current working directory not yet supported");
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    auto oldpath_abs = capio_absolute(oldpath);
    auto newpath_abs = capio_absolute(newpath);

    if (is_prefix(oldpath_abs, newpath_abs)) { // TODO: The check is more complex
        errno   = EINVAL;
        *result = -errno;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }

    if (is_capio_path(oldpath_abs)) {
        rename_capio_path(oldpath_abs, newpath_abs);
        auto res = rename_request(tid, oldpath_abs, newpath_abs);
        *result  = (res < 0 ? -errno : res);
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    } else {
        if (is_capio_path(newpath_abs)) {
            std::filesystem::copy(oldpath_abs, newpath_abs);
            *result = -errno;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        } else {
            return CAPIO_POSIX_SYSCALL_SKIP;
        }
    }
}

#endif

#endif // CAPIO_POSIX_HANDLERS_RENAME_HPP
