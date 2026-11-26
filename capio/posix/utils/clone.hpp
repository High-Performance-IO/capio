#ifndef CAPIO_POSIX_UTILS_CLONE_HPP
#define CAPIO_POSIX_UTILS_CLONE_HPP

#include <linux/sched.h>

#include "calf/SyscallLogger.h"
#include "common/syscall.hpp"
#include "data.hpp"
#include "requests.hpp"

/*
 * Process creation. A new process cannot become a client as soon as it exists:
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

/**
 * @brief Called in a new process, before it does anything. A vfork child, or the fork copy of a
 * thread that had spawned one, has its parent in another thread: it never becomes a client, and
 * must not touch the flag, which a vfork child shares with its parent.
 */
inline void defer_new_process() {
    if (!in_vfork_child()) {
        process_needs_setup = true;
    }
}

/// @brief Called before a system call, to notice the creation of a process sharing memory
inline void note_process_creation(const long number, const long arg0) {
    vfork_parent_tid = 0; // the parent runs again, if it had a vfork child: it has execed or exited
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

/**
 * Initialize the required data structures for the new child thread, and then proceed to execute a
 * handshake request, using the DEFAULT CAPIO APP NAME is no app name is provided.
 * The wait parameter is used to await for an explicit reply from the server that this client
 * has been successfully initialized on the server side, whenever the initialize_new_thread routine
 * is called after a SYS_clone is issued.
 */
inline void initialize_new_thread(const bool wait = false) {
    SET_CALF_SYSCALL_HANDLER(syscall_no_intercept);

    // Its system calls (the shared memory and the semaphores of the queues) are on no CAPIO file,
    // and must not reach CAPIO's handlers
    const bool flag_was_set   = syscall_no_intercept_flag;
    syscall_no_intercept_flag = true;

    const long tid = syscall_no_intercept(SYS_gettid);
    const long pid = syscall_no_intercept(SYS_getpid);

    const auto capio_app_name = get_capio_app_name();

    START_LOG(tid, "call(tid=%d, pid=%d, app_name=%s)", tid, pid, capio_app_name);

    if (pid == tid) {
        // A new process has no other threads, whatever its parent was doing
        requests_being_written = 0;
        resume_requests();
    }
    initialize_data_queues(tid);
    init_client(tid);

    handshake_named_request(tid, pid, capio_app_name, wait);
    LOG("Starting child thread %d", tid);
    syscall_no_intercept_flag = flag_was_set;
}

/**
 * FS variant: tell the server the descriptors that a forked child got, as the server knows them by
 * process and they are new to it. They do not make the child a producer, as it did not create
 * them. Called when the child becomes a client, at its first system call that CAPIO handles: the
 * fork has released the table by then.
 */
inline void announce_inherited_descriptors_fs() {
    const long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call()");
    for (const int fd : fd_table->fds()) {
        create_request_fs(fd_table->path(fd), fd, CAPIO_FS_NOT_PRODUCED, tid);
    }
}

inline void hook_clone_parent(long child_tid) {
    // syscall_intercept invokes this hook even when the clone fails (e.g. clone3 returning
    // -ENOSYS under seccomp filters, after which glibc falls back to clone): do not register
    // a nonexistent child with the server.
    if (child_tid <= 0) {
        return;
    }
    const auto tid = syscall_no_intercept(SYS_gettid);
    clone_request(tid, child_tid);
}

/**
 * Called in the child after a successful clone/clone3. When the child is a new thread, wait for
 * the server to acknowledge the handshake. When it is a new process (fork semantics, i.e. no
 * CLONE_THREAD, as issued by fork() on architectures without SYS_fork such as aarch64), behave
 * like fork_handler and do not wait: the child may be the one that is about to exec the CAPIO
 * server, so waiting for a reply would deadlock.
 */
inline void hook_clone_child() {
    const long pid = syscall_no_intercept(SYS_getpid);
    const long tid = syscall_no_intercept(SYS_gettid);
    if (pid == tid) {
        defer_new_process(); // see process creation above
        return;
    }
    initialize_new_thread(true);
}

#endif // CAPIO_POSIX_UTILS_CLONE_HPP
