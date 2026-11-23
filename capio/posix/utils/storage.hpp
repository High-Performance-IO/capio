#ifndef CAPIO_POSIX_UTILS_STORAGE_HPP
#define CAPIO_POSIX_UTILS_STORAGE_HPP

#include <cerrno>

/**
 * The result of syscall_no_intercept, which is the negated error on failure, in the form the
 * handlers return: -1, with the error in errno.
 */
inline long capio_kernel_result(const long result) {
    if (result < 0) {
        errno = static_cast<int>(-result);
        return -1;
    }
    return result;
}

// Pick an expression of the memory or the FS variant at compile time; statements use #if
// CAPIO_STORAGE_CALL(memory_call, fs_call)
#if CAPIO_MEM_STREAM
#define CAPIO_STORAGE_CALL(memory_call, fs_call) memory_call
#else
#define CAPIO_STORAGE_CALL(memory_call, fs_call) fs_call
#endif

#endif // CAPIO_POSIX_UTILS_STORAGE_HPP
