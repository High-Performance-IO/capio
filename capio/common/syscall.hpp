#ifndef CAPIO_COMMON_SYSCALL_HPP
#define CAPIO_COMMON_SYSCALL_HPP

#include <syscall.h>

#ifdef __CAPIO_POSIX

#include <libsyscall_intercept_hook_point.h>

#define capio_syscall syscall_no_intercept

/* Allows CAPIO to deactivate syscalls hooking. */
thread_local bool syscall_no_intercept_flag = false;

inline char *syscall_no_intercept_realpath(const char *path, char *resolved) {
    syscall_no_intercept_flag = true;
    char *res                 = realpath(path, resolved);
    syscall_no_intercept_flag = false;
    return res;
}

#define capio_realpath syscall_no_intercept_realpath
#else
#define capio_syscall syscall
#define capio_realpath realpath
#endif

// The gettid function has been introduced in glibc v2.30
#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#define gettid() capio_syscall(SYS_gettid)
#endif

#endif // CAPIO_COMMON_SYSCALL_HPP
