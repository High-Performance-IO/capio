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

inline pid_t capio_clone(int flags, long parent_tid) {

    CAPIO_DBG("capio_clone: PARENT_TID[%d] FLAGS[%d]: enter\n", parent_tid, flags);

    if ((flags & CLONE_THREAD) != CLONE_THREAD) {

        CAPIO_DBG("capio_clone: PARENT_TID[%d] FLAGS[%d]: process creation\n", parent_tid, flags);

        auto pid = static_cast<pid_t>(syscall_no_intercept(SYS_fork));
        if (pid == 0) { //child
            auto child_tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

            CAPIO_DBG("capio_clone: PARENT_TID[%d] FLAGS[%d]: created child process %d\n", parent_tid, flags, child_tid);

            mtrace_init(child_tid);

            CAPIO_DBG("capio_clone: PARENT_TID[%d] FLAGS[%d]: initialized child process %d\n", parent_tid, flags, child_tid);

            clone_request(parent_tid, child_tid);

            CAPIO_DBG("capio_clone: PARENT_TID[%d] FLAGS[%d]: process creation ending, child process %ld returns 0\n", parent_tid, flags, child_tid);

            return 0;
        } else {

            CAPIO_DBG("capio_clone: PARENT_TID[%d] FLAGS[%d]: process creation ending, parent process returns %d\n", parent_tid, flags, pid);

            return pid;
        }
    } else {

        CAPIO_DBG("capio_clone: PARENT_TID[%d] FLAGS[%d]: thread creation, return 1\n", parent_tid, flags);

        return 1;
    }
}

int clone_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long tid){

    int res;
    res = capio_clone(static_cast<int>(arg0), tid);
    if (res == 1)
        return 1;
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}


#endif // CAPIO_POSIX_HANDLERS_CLONE_HPP
