#ifndef CAPIO_POSIX_HANDLERS_RENAME_HPP
#define CAPIO_POSIX_HANDLERS_RENAME_HPP

#if defined(SYS_rename)

#include "utils/filesystem.hpp"

int rename_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result,
                   const pid_t tid) {
    std::filesystem::path oldpath(reinterpret_cast<const char *>(arg0));
    std::filesystem::path newpath(reinterpret_cast<const char *>(arg1));

    START_LOG(tid, "call(oldpath=%s, newpath=%s)", oldpath.c_str(), newpath.c_str());
    LOG("Compute paths");
    auto oldpath_abs = capio_absolute(oldpath);
    LOG("oldpath absolute: %s", oldpath_abs.c_str());
    auto newpath_abs = capio_absolute(newpath);
    LOG("newpath absolute: %s", newpath_abs.c_str());

    if (is_prefix(oldpath_abs, newpath_abs)) {
        // TODO: The check is more complex
        errno   = EINVAL;
        *result = -errno;
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    LOG("newpath is not prefix of old");

    if (is_capio_path(oldpath_abs)) {
        LOG("Oldpath is capio_path");
        rename_capio_path(oldpath_abs, newpath_abs);
    }

    if (is_capio_path(oldpath_abs) || is_capio_path(newpath_abs)) {
        LOG("Either old or new or both paths are capio_paths. sending request");
        rename_request(oldpath_abs, newpath_abs, tid);
    }

    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_rename
#endif // CAPIO_POSIX_HANDLERS_RENAME_HPP