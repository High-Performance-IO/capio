#ifndef CAPIO_POSIX_HANDLERS_FORK_HPP
#define CAPIO_POSIX_HANDLERS_FORK_HPP

#include "utils/clone.hpp"
#include "utils/requests.hpp"

#if defined(SYS_fork)

int fork_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto pid = static_cast<pid_t>(syscall_no_intercept(SYS_fork));
    START_LOG(syscall_no_intercept(SYS_gettid), "call(pid=%ld)", pid);

    if (pid == 0) {          // child
        defer_new_process(); // see utils/clone.hpp
        *result = 0;
    } else {
        *result = pid;
    }

    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // SYS_fork

/*
 * clone, clone3 and vfork go to the kernel: their handlers only note a new process that will run
 * in the memory of the caller (see utils/clone.hpp). They must not allocate: fork() makes its clone
 * while holding the malloc lock.
 */
#ifdef SYS_clone
int clone_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    // clone(flags, stack, parent_tid, child_tid, tls)
    const auto flags = static_cast<unsigned long>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(flags=0x%lx)", flags);

    note_clone(flags);
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

#ifdef SYS_clone3
int clone3_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    // clone3(args, size)
    const auto *args = reinterpret_cast<const struct clone_args *>(arg0);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(args=0x%lx)", arg0);

    if (args != nullptr) {
        note_clone(args->flags);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

#ifdef SYS_vfork
int vfork_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call()");

    note_clone(CLONE_VM);
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

/// @brief Whether @p handler is one of a system call that creates a process or a thread
inline bool creates_a_process(const CPHandler_t handler) {
    return false
#ifdef SYS_fork
           || handler == fork_handler
#endif
#ifdef SYS_clone
           || handler == clone_handler
#endif
#ifdef SYS_clone3
           || handler == clone3_handler
#endif
#ifdef SYS_vfork
           || handler == vfork_handler
#endif
        ;
}

#endif // CAPIO_POSIX_HANDLERS_FORK_HPP
