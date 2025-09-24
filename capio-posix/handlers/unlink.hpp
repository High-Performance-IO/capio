#ifndef CAPIO_POSIX_HANDLERS_UNLINK_HPP
#define CAPIO_POSIX_HANDLERS_UNLINK_HPP

#include "utils/common.hpp"

#if defined(SYS_unlink)
int unlink_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    std::string_view pathname(reinterpret_cast<const char *>(arg0));
    auto tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    START_LOG(tid, "call(path=%s)", pathname.data());

    if (is_capio_path(pathname)) {
        LOG("Deleting path");
        delete_capio_path(pathname.data());
    }

    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif // SYS_unlink

#if defined(SYS_unlinkat)
int unlinkat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    auto tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    START_LOG(tid, "call(path=%s)", pathname.data());
    auto path = capio_posix_realpath(pathname);
    if (is_capio_path(path)) {
        LOG("Deleting path");
        delete_capio_path(path);
    }

    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif // SYS_unlinkat

#endif // CAPIO_POSIX_HANDLERS_UNLINK_HPP