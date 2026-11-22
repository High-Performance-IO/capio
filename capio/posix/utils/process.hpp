#ifndef CAPIO_POSIX_UTILS_PROCESS_HPP
#define CAPIO_POSIX_UTILS_PROCESS_HPP

#include <linux/sched.h>

#include "common/syscall.hpp"

/*
 * Process creation in the FS variant. A new process cannot become a client as soon as it exists:
 *
 * - after fork, glibc still holds the malloc lock in the child, and becoming a client allocates.
 *   The child becomes one at its first system call that CAPIO handles, when the lock is free.
 * - after vfork or posix_spawn, the child runs in the memory of its parent, thread-local variables
 *   included, until it execs. It must not become a client at all: it would replace the queues of
 *   its parent. Its system calls go to the kernel, and the program it execs becomes a client.
 */

inline thread_local long vfork_parent_tid    = 0;
inline thread_local bool process_needs_setup = false;

/// @brief Whether the caller is a child running in the memory of its parent, until it execs
inline bool in_vfork_child() {
    return vfork_parent_tid != 0 && syscall_no_intercept(SYS_gettid) != vfork_parent_tid;
}

/// @brief Called in a new process, before it does anything
inline void defer_new_process(const long tid) {
    if (vfork_parent_tid != 0 && tid != vfork_parent_tid) {
        // Either a vfork child, or a fork copy of a thread that had spawned one. Only the latter
        // owns the variable, but in both cases the parent is another thread.
        return;
    }
    process_needs_setup = true;
}

/// @brief Called before a system call, to notice the creation of a process sharing memory
inline void note_process_creation(const long number, const long arg0) {
    if (vfork_parent_tid != 0) {
        vfork_parent_tid = 0; // the parent runs again: the child has execed or exited
    }
    unsigned long long flags = 0;
#ifdef SYS_vfork
    if (number == SYS_vfork) {
        flags = CLONE_VM;
    }
#endif
    if (number == SYS_clone) {
        flags = static_cast<unsigned long>(arg0);
    }
#ifdef SYS_clone3
    if (number == SYS_clone3) {
        flags = reinterpret_cast<const struct clone_args *>(arg0)->flags;
    }
#endif
    if ((flags & CLONE_VM) != 0 && (flags & CLONE_THREAD) == 0) {
        vfork_parent_tid = syscall_no_intercept(SYS_gettid);
    }
}

#endif // CAPIO_POSIX_UTILS_PROCESS_HPP
