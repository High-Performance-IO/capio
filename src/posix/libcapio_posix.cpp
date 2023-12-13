/**
 * Capio log level.
 * if -1, and capio logging is enable everything is logged, otherwise, only
 * logs up to CAPIO_MAX_LOG_LEVEL function calls
 */

#include <array>
#include <string>

#include <asm-generic/unistd.h>

#include "capio/env.hpp"
#include "capio/syscall.hpp"

#include "utils/clone.hpp"
#include "utils/filesystem.hpp"
#include "utils/snapshot.hpp"

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

#ifdef SYS_access
    _syscallTable[SYS_access] = access_handler;
#endif
#ifdef SYS_chdir
    _syscallTable[SYS_chdir] = chdir_handler;
#endif
#ifdef SYS_chmod
    _syscallTable[SYS_chmod] = fchmod_handler;
#endif
#ifdef SYS_chown
    _syscallTable[SYS_chown] = fchown_handler;
#endif
#ifdef SYS_close
    _syscallTable[SYS_close] = close_handler;
#endif
#ifdef SYS_creat
    _syscallTable[SYS_creat] = creat_handler;
#endif
#ifdef SYS_dup
    _syscallTable[SYS_dup] = dup_handler;
#endif
#ifdef SYS_dup2
    _syscallTable[SYS_dup2] = dup2_handler;
#endif
#ifdef SYS_dup3
    _syscallTable[SYS_dup3] = dup3_handler;
#endif
#ifdef SYS_execve
    _syscallTable[SYS_execve] = execve_handler;
#endif
#ifdef SYS_exit
    _syscallTable[SYS_exit] = exit_handler;
#endif
#ifdef SYS_exit_group
    _syscallTable[SYS_exit_group] = exit_handler;
#endif
#ifdef SYS_faccessat
    _syscallTable[SYS_faccessat] = faccessat_handler;
#endif
#ifdef SYS_faccessat2
    _syscallTable[SYS_faccessat2] = faccessat_handler;
#endif
#ifdef SYS_fcntl
    _syscallTable[SYS_fcntl] = fcntl_handler;
#endif
#ifdef SYS_fgetxattr
    _syscallTable[SYS_fgetxattr] = fgetxattr_handler;
#endif
#ifdef SYS_flistxattr
    _syscallTable[SYS_flistxattr] = not_implemented_handler;
#endif
#ifdef SYS_fork
    _syscallTable[SYS_fork] = fork_handler;
#endif
#ifdef SYS_fstat
    _syscallTable[SYS_fstat] = fstat_handler;
#endif
#ifdef SYS_fstatfs
    _syscallTable[SYS_fstatfs] = fstatfs_handler;
#endif
#ifdef SYS_getcwd
    _syscallTable[SYS_getcwd] = getcwd_handler;
#endif
#ifdef SYS_getdents
    _syscallTable[SYS_getdents] = getdents_handler;
#endif
#ifdef SYS_getdents64
    _syscallTable[SYS_getdents64] = getdents64_handler;
#endif
#ifdef SYS_getxattr
    _syscallTable[SYS_getxattr] = not_implemented_handler;
#endif
#ifdef SYS_ioctl
    _syscallTable[SYS_ioctl] = ioctl_handler;
#endif
#ifdef SYS_lgetxattr
    _syscallTable[SYS_lgetxattr] = not_implemented_handler;
#endif
#ifdef SYS_lseek
    _syscallTable[SYS_lseek] = lseek_handler;
#endif
#ifdef SYS_lstat
    _syscallTable[SYS_lstat] = lstat_handler;
#endif
#ifdef SYS_mkdir
    _syscallTable[SYS_mkdir] = mkdir_handler;
#endif
#ifdef SYS_mkdirat
    _syscallTable[SYS_mkdirat] = mkdirat_handler;
#endif
#ifdef SYS_newfstatat
    _syscallTable[SYS_newfstatat] = fstatat_handler;
#endif
#ifdef SYS_openat
    _syscallTable[SYS_openat] = openat_handler;
#endif
#ifdef SYS_read
    _syscallTable[SYS_read] = read_handler;
#endif
#ifdef SYS_rename
    _syscallTable[SYS_rename] = rename_handler;
#endif
#ifdef SYS_stat
    _syscallTable[SYS_stat] = lstat_handler;
#endif
#ifdef SYS_statx
    _syscallTable[SYS_statx] = statx_handler;
#endif
#ifdef SYS_unlink
    _syscallTable[SYS_unlink] = unlink_handler;
#endif
#ifdef SYS_unlinkat
    _syscallTable[SYS_unlinkat] = unlinkat_handler;
#endif
#ifdef SYS_write
    _syscallTable[SYS_write] = write_handler;
#endif
#ifdef SYS_writev
    _syscallTable[SYS_writev] = writev_handler;
#endif
#ifdef SYS_rmdir
    _syscallTable[SYS_rmdir] = rmdir_handler;
#endif

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
    init_filesystem();

    long tid = syscall_no_intercept(SYS_gettid);

    int *fd_shm = get_fd_snapshot(tid);
    if (fd_shm != nullptr) {
        initialize_from_snapshot(fd_shm, tid);
    }

    init_process(tid);

    intercept_hook_point_clone_child  = hook_clone_child;
    intercept_hook_point_clone_parent = hook_clone_parent;
    intercept_hook_point              = hook;
    START_SYSCALL_LOGGING();
}
