#include <cerrno>

#ifndef CAPIO_FUNCTIONS_H
#define CAPIO_FUNCTIONS_H

int posix_return_value(long res, long *result) {
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return POSIX_SYSCALL_HANDLED_BY_CAPIO;
    }
    return POSIX_SYSCALL_TO_HANDLE_BY_KERNEL;
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
