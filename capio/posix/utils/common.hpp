#ifndef CAPIO_FUNCTIONS_H
#define CAPIO_FUNCTIONS_H
#include <cerrno>
#include <climits>
#include <sys/auxv.h>

#include "calf/SyscallLogger.h"

/**
 * Largest number of bytes a single read, write or copy system call moves, as in the kernel: the
 * caller asks again for the rest. The kernel limit, MAX_RW_COUNT, is INT_MAX rounded down to a
 * page, and is not exported to user space: it is computed here from the page size, which getauxval
 * reads without a system call.
 *
 * @return the largest number of bytes a call moves
 */
inline off64_t capio_max_rw_count() {
    static const off64_t max_count = INT_MAX & ~(static_cast<off64_t>(getauxval(AT_PAGESZ)) - 1);
    return max_count;
}

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

#endif // CAPIO_FUNCTIONS_H
