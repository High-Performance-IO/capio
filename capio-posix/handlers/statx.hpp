#ifndef CAPIO_POSIX_HANDLERS_STATX_HPP
#define CAPIO_POSIX_HANDLERS_STATX_HPP

#if defined(SYS_statx)

#include "utils/common.hpp"

inline int capio_statx(int dirfd, const std::string_view &pathname, int flags, int mask,
                       struct statx *statxbuf, pid_t tid) {
    START_LOG(tid, "call(dirfd=%d, pathname=%s, flags=%d, mask=%d, statxbuf=0x%08x)", dirfd,
              pathname.data(), flags, mask, statxbuf);

    if (!is_capio_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    consent_request_cache_fs->consent_request(pathname, tid, __FUNCTION__);

    return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
}

int statx_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto dirfd = static_cast<int>(arg0);
    const std::string_view pathname(reinterpret_cast<const char *>(arg1));
    auto flags = static_cast<int>(arg2);
    auto mask  = static_cast<int>(arg3);
    auto *buf  = reinterpret_cast<struct statx *>(arg4);
    auto tid   = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    return posix_return_value(capio_statx(dirfd, pathname, flags, mask, buf, tid), result);
}

#endif // SYS_statx
#endif // CAPIO_POSIX_HANDLERS_STATX_HPP