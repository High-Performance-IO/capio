#ifndef CAPIO_FUNCTIONS_H
#define CAPIO_FUNCTIONS_H
#include <cerrno>
#include <climits>
#include <stdexcept>
#include <string>
#include <sys/auxv.h>

#include "calf/SyscallLogger.h"
#include "utils/tid.hpp"

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

/// @brief Thrown by a system call on a CAPIO file that this variant of the library does not
/// handle yet. The message names the system call and the variant.
class CapioUnimplementedException : public std::logic_error {
    static constexpr const char *variant = MEMORY_MODE("memory variant (libcapio_posix.so)")
        FS_MODE("file system variant (libcapio_posix_fs.so)");

  public:
    explicit CapioUnimplementedException(const std::string &syscall)
        : std::logic_error(syscall + " is not implemented yet by the " + variant) {}
};

int posix_return_value(long res, long *result) {
    START_LOG(capio_tid(), "cal(res=%ld)", res);
    if (res != CAPIO_POSIX_SYSCALL_REQUEST_SKIP) {
        *result = (res < 0 ? -errno : res);
        LOG("SYSCALL handled by capio. errno is: %s", res < 0 ? strerror(-errno) : "none");
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    LOG("SYSCALL delegated to the kernel");
    return CAPIO_POSIX_SYSCALL_SKIP;
}

/**
 * The result of syscall_no_intercept, which is the negated error on failure, in the form the
 * handlers return: -1, with the error in errno.
 *
 * TODO: use one convention for the return values of all handlers, with no result value that
 * doubles as a sentinel (-2 is both CAPIO_POSIX_SYSCALL_REQUEST_SKIP and -ENOENT), and remove this
 * conversion. See https://github.com/High-Performance-IO/capio/issues/311
 */
inline long capio_kernel_result(const long result) {
    if (result < 0) {
        errno = static_cast<int>(-result);
        return -1;
    }
    return result;
}

#endif // CAPIO_FUNCTIONS_H
