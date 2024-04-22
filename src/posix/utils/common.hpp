#include <cerrno>

#ifndef CAPIO_FUNCTIONS_H
#define CAPIO_FUNCTIONS_H

int posix_return_value(long res, long *result) {
    START_LOG(capio_syscall(SYS_gettid), "cal(res=%ld)", res);
    if (res != CAPIO_POSIX_SYSCALL_REQUEST_SKIP) {
        *result = (res < 0 ? -errno : res);
        LOG("SYSCALL handled by capio. errno is: %s", res < 0 ? strerror(-errno) : "none");
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    LOG("SYSCALL delegated to the kernel");
    return CAPIO_POSIX_SYSCALL_SKIP;
}

inline off64_t dirent_round(off64_t bytes) {
    off64_t res     = 0;
    off64_t ld_size = CAPIO_THEORETICAL_SIZE_DIRENT64;

    while (res + ld_size <= bytes) {
        res += ld_size;
    }
    return res;
}

#endif // CAPIO_FUNCTIONS_H
