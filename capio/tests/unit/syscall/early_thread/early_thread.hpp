#ifndef CAPIO_TESTS_UNIT_SYSCALL_EARLY_THREAD_HPP
#define CAPIO_TESTS_UNIT_SYSCALL_EARLY_THREAD_HPP

#include <cstddef>

/**
 * @brief Have the thread that this library starts before libcapio is set up read up to @p size
 * bytes of @p path into @p buffer. It does it once: the thread then ends.
 * @return The number of bytes read, or minus the error of the open or the read; -EALREADY if it
 * was asked before, -ESRCH if the thread could not be started.
 */
long capio_test_early_thread_read(const char *path, char *buffer, size_t size);

#endif // CAPIO_TESTS_UNIT_SYSCALL_EARLY_THREAD_HPP
