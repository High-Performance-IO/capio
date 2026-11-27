#ifndef CAPIO_POSIX_UTILS_TID_HPP
#define CAPIO_POSIX_UTILS_TID_HPP

#include <libsyscall_intercept_hook_point.h>
#include <sys/syscall.h>

/**
 * The id of the calling thread, kept per thread: gettid is a system call (about 50 ns), and the
 * handlers need the id for nearly every request and log line.
 *
 * initialize_new_thread sets it, in every thread libcapio sets up: the main one, the children of
 * clone and fork, and the threads started before libcapio. Elsewhere (the unit tests), the first
 * call asks the kernel.
 *
 * NOTE: a vfork child runs on the TLS of its parent, so it sees the id of the parent, as it sees
 * the parent's queues and caches: syscall_intercept calls no hook for vfork.
 */
inline thread_local long capio_cached_tid = -1;

/// @brief Ask the kernel for the id of the calling thread, and keep it
inline long capio_reset_tid() {
    capio_cached_tid = syscall_no_intercept(SYS_gettid);
    return capio_cached_tid;
}

/// @brief The id of the calling thread
inline long capio_tid() { return capio_cached_tid >= 0 ? capio_cached_tid : capio_reset_tid(); }

#endif // CAPIO_POSIX_UTILS_TID_HPP
