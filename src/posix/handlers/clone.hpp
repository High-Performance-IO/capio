#ifndef CAPIO_POSIX_HANDLERS_CLONE_HPP
#define CAPIO_POSIX_HANDLERS_CLONE_HPP

#include "globals.hpp"
#include "utils/requests.hpp"

/*
 * From "The Linux Programming Interface: A Linux and Unix System Programming Handbook", by Micheal Kerrisk:
 * "Within the kernel, fork(), vfork(), and clone() are ultimately
 * implemented by the same function (do_fork() in kernel/fork.c).
 * At this level, cloning is much closer to forking: sys_clone() doesn’t
 * have the func and func_arg arguments, and after the call, sys_clone()
 * returns in the child in the same manner as fork(). The main text
 * describes the clone() wrapper function that glibc provides for sys_clone().
 * This wrapper function invokes func after sys_clone() returns in the child."
*/

static void hook_clone_parent(long child_pid) {
    auto parent_pid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));
    START_LOG(parent_pid, "call(parent_tid=%ld)", parent_pid);
    mtrace_init(child_pid);
    clone_request(parent_pid, child_pid);

}

#endif // CAPIO_POSIX_HANDLERS_CLONE_HPP
