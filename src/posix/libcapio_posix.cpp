/**
 * Capio log level.
 * if -1, and capio logging is enable everything is logged, otherwise, only
 * logs up to CAPIO_MAX_LOG_LEVEL function calls
 */

#include <asm-generic/unistd.h>

#include <array>
#include <string>
#include <unordered_map>

#include "capio/env.hpp"
#include "globals.hpp"
#include "handlers.hpp"

/**
 * Handler for syscall not handled and interrupt syscall_intercept
 */
static int not_handled_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                               long *result) {
    return 1;
}

/**
 * Handler for syscall handled, but not yet implemented and interrupt
 * syscall_intercept
 */
static int not_implemented_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                                   long *result) {
    errno   = ENOTSUP;
    *result = -errno;
    return 0;
}

static constexpr std::array<CPHandler_t, __NR_syscalls> build_syscall_table() {
    std::array<CPHandler_t, __NR_syscalls> _syscallTable{0};

    for (int i = 0; i < __NR_syscalls; i++) {
        _syscallTable[i] = not_handled_handler;
    }

    _syscallTable[SYS_access]     = access_handler;
    _syscallTable[SYS_chdir]      = chdir_handler;
    _syscallTable[SYS_chmod]      = fchmod_handler;
    _syscallTable[SYS_chown]      = fchown_handler;
    _syscallTable[SYS_close]      = close_handler;
    _syscallTable[SYS_creat]      = creat_handler;
    _syscallTable[SYS_dup]        = dup_handler;
    _syscallTable[SYS_dup2]       = dup2_handler;
    _syscallTable[SYS_execve]     = execve_handler;
    _syscallTable[SYS_exit]       = exit_handler;
    _syscallTable[SYS_exit_group] = exit_handler;
    _syscallTable[SYS_faccessat]  = faccessat_handler;
    _syscallTable[SYS_faccessat2] = faccessat_handler;
    _syscallTable[SYS_fcntl]      = fcntl_handler;
    _syscallTable[SYS_fgetxattr]  = fgetxattr_handler;
    _syscallTable[SYS_flistxattr] = not_implemented_handler;
    _syscallTable[SYS_fork]       = fork_handler;
    _syscallTable[SYS_fstat]      = fstat_handler;
    _syscallTable[SYS_fstatfs]    = fstatfs_handler;
    _syscallTable[SYS_getcwd]     = getcwd_handler;
    _syscallTable[SYS_getdents]   = getdents_handler;
    _syscallTable[SYS_getdents64] = getdents64_handler;
    _syscallTable[SYS_getxattr]   = not_implemented_handler;
    _syscallTable[SYS_ioctl]      = ioctl_handler;
    _syscallTable[SYS_lgetxattr]  = not_implemented_handler;
    _syscallTable[SYS_lseek]      = lseek_handler;
    _syscallTable[SYS_lstat]      = lstat_handler;
    _syscallTable[SYS_mkdir]      = mkdir_handler;
    _syscallTable[SYS_mkdirat]    = mkdirat_handler;
    _syscallTable[SYS_newfstatat] = fstatat_handler;
    _syscallTable[SYS_openat]     = openat_handler;
    _syscallTable[SYS_read]       = read_handler;
    _syscallTable[SYS_rename]     = rename_handler;
    _syscallTable[SYS_stat]       = lstat_handler;
    _syscallTable[SYS_statx]      = statx_handler;
    _syscallTable[SYS_unlink]     = unlink_handler;
    _syscallTable[SYS_unlinkat]   = unlinkat_handler;
    _syscallTable[SYS_write]      = write_handler;
    _syscallTable[SYS_writev]     = writev_handler;
    _syscallTable[SYS_rmdir]      = rmdir_handler;

    return _syscallTable;
}

static int hook(long syscall_number, long arg0, long arg1, long arg2, long arg3, long arg4,
                long arg5, long *result) {
    static const std::array<CPHandler_t, __NR_syscalls> syscallTable = build_syscall_table();
    static const char *capio_dir                                     = std::getenv("CAPIO_DIR");

#ifdef CAPIOLOG
    CAPIO_LOG_LEVEL = get_capio_log_level();
#endif

    // If the flag is set to true, CAPIO will not
    // intercept the system calls
    if (syscall_no_intercept_flag) {
        return 1;
    }

    START_LOG(syscall_no_intercept(SYS_gettid), "call(syscall_number=%ld)", syscall_number);

    // NB: if capio dir is not set as environment variable,
    // then capio will not intercept the system calls
    if (capio_dir == nullptr) {
        LOG("CAPIO_DIR env var not set. returning control to kernel");
        return 1;
    }

    return syscallTable[syscall_number](arg0, arg1, arg2, arg3, arg4, arg5, result);
}

static __attribute__((constructor)) void init() {
    init_client();
    init_data_plane();
    char *buf = (char *) malloc(PATH_MAX * sizeof(char));
    syscall_no_intercept(SYS_getcwd, buf, PATH_MAX);
    current_dir = new std::string(buf);
    mtrace_init(syscall_no_intercept(SYS_gettid));

    intercept_hook_point_clone_child  = hook_clone_child;
    intercept_hook_point_clone_parent = hook_clone_parent;
    intercept_hook_point              = hook;
}