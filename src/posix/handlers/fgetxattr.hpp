#ifndef CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
#define CAPIO_POSIX_HANDLERS_FGETXATTR_HPP

#include "globals.hpp"

inline int capio_fgetxattr(int fd, const std::string &name, void *value, size_t size, long tid) {
    START_LOG(tid, "call(name=%s, value=0x%08x, size=%ld)", name.c_str(), value, size);

    auto it = files->find(fd);
    if (it != files->end()) {
        if (std::equal(name.begin(), name.end(), "system.posix_acl_access")) {
            errno = ENODATA;
            return -1;
        } else {
            ERR_EXIT("fgetxattr with name %s is not yet supported in CAPIO", name.c_str());
        }
    } else {
        return -2;
    }
}

int fgetxattr_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    std::string name(reinterpret_cast<const char *>(arg1));
    auto *value = reinterpret_cast<void *>(arg2);
    auto size = static_cast<size_t>(arg3);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(name=%s, value=0x%08x, size=%ld)", name.c_str(), value, size);

    int res = capio_fgetxattr(static_cast<int>(arg0), name, value, size, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_FGETXATTR_HPP
