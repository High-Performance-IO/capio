#include <cerrno>

#ifndef CAPIO_FUNCTIONS_H
#define CAPIO_FUNCTIONS_H

int posix_return_value(long res, long *result) {
    START_LOG(capio_syscall(SYS_gettid), "cal(res=%ld)", res);
    if (res != POSIX_SYSCALL_REQUEST_SKIP) {
        *result = (res < 0 ? -errno : res);
        LOG("SYSCALL handled by capio. errno is: %s", res < 0 ? strerror(-errno) : "none");
        return POSIX_SYSCALL_SUCCESS;
    }
    LOG("SYSCALL delegated to the kernel");
    return POSIX_SYSCALL_SKIP;
}

inline std::string absolute(const std::string &path) {
    return is_absolute(&path) ? path : *capio_posix_realpath(&path);
}

std::string get_capio_parent_dir(const std::string &path) {
    auto pos = path.rfind('/');
    return path.substr(0, pos);
}

inline off64_t round(off64_t bytes, bool is_getdents64) {
    off64_t res = 0;
    off64_t ld_size;
    ld_size = THEORETICAL_SIZE_DIRENT64;

    while (res + ld_size <= bytes) {
        res += ld_size;
    }
    return res;
}

#endif // CAPIO_FUNCTIONS_H
