#ifndef CAPIO_POSIX_HANDLERS_UNLINK_HPP
#define CAPIO_POSIX_HANDLERS_UNLINK_HPP

#include "utils/common.hpp"

#if defined(SYS_unlink)
inline int unlink_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                          long *result, const pid_t tid) {
    std::string_view pathname(reinterpret_cast<const char *>(arg0));

    START_LOG(tid, "call(path=%s)", pathname.data());

    if (is_capio_path(pathname)) {
        LOG("Deleting path");
        delete_capio_path(pathname.data());
    }

    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif // SYS_unlink

#if defined(SYS_unlinkat)
inline int unlinkat_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                            long *result, const pid_t tid) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));

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