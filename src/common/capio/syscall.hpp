#ifndef CAPIO_COMMON_SYSCALL_HPP
#define CAPIO_COMMON_SYSCALL_HPP

#ifdef __CAPIO_POSIX
#include <libsyscall_intercept_hook_point.h>
#define capio_syscall syscall_no_intercept
#else
#include <syscall.h>
#define capio_syscall syscall
#endif

#endif // CAPIO_COMMON_SYSCALL_HPP
