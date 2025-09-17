#ifndef CAPIO_POSIX_HANDLERS_CHDIR_HPP
#define CAPIO_POSIX_HANDLERS_CHDIR_HPP
#include "utils/requests.hpp"

#if defined(SYS_chdir)

#include "utils/filesystem.hpp"

inline int chdir_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result,
                  const pid_t tid) {
    const std::string_view pathname(reinterpret_cast<const char *>(arg0));
    START_LOG(tid, "call(path=%s)", pathname.data());

    syscall_no_intercept_flag = true;
    if (!is_capio_path(pathname)) {
        LOG("Path %s is forbidden: skip", pathname.data());
        syscall_no_intercept_flag = false;
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    std::filesystem::path path(pathname);
    if (path.is_relative()) {
        path = capio_posix_realpath(path);
    }

    consent_request_cache_fs->consent_request(path, tid, __FUNCTION__);

    syscall_no_intercept_flag = false;
    // if not a capio path, then control is given to kernel
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_chdir
#endif // CAPIO_POSIX_HANDLERS_CHDIR_HPP