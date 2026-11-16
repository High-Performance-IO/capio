#ifndef CAPIO_COMMON_SYSCALL_HPP
#define CAPIO_COMMON_SYSCALL_HPP

#include <syscall.h>

#ifdef __CAPIO_POSIX

#if defined(__riscv)

#include <libvpoline.h>
static syscall_no_intercept_t syscall_no_intercept = nullptr;

/*
 * Mirror of syscall_intercept's helper (to be added on vpoline)
 *
 * syscall_error_code - examines a return value from
 * syscall_no_intercept, and returns an error code if said
 * return value indicates an error.
 */
static inline int syscall_error_code(long result) {
    if (result < 0 && result >= -0x1000)
        return (int)-result;
    return 0;
}

#else

#include <libsyscall_intercept_hook_point.h>

#endif

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

/*
 * Code of one variant of the library only, as a block: MEMORY_MODE({ ... }) is kept in
 * libcapio_posix.so, where files are in the memory of the server, FS_MODE({ ... }) in
 * libcapio_posix_fs.so, where they are on the file system. The other one expands to nothing.
 */
#if CAPIO_MEM_STREAM
#define MEMORY_MODE(...) __VA_ARGS__
#define FS_MODE(...)
#else
#define MEMORY_MODE(...)
#define FS_MODE(...) __VA_ARGS__
#endif
#else
#define capio_syscall syscall
#define capio_realpath realpath
#endif

// The gettid function has been introduced in glibc v2.30
#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#define gettid() capio_syscall(SYS_gettid)
#endif

#endif // CAPIO_COMMON_SYSCALL_HPP
