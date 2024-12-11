/**
 * Capio log level.
 * if -1, and capio logging is enable everything is logged, otherwise, only
 * logs up to CAPIO_MAX_LOG_LEVEL function calls
 */

#include <array>
#include <string>

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

static constexpr long CAPIO_NR_SYSCALLS = 1 + std::max({
#ifdef SYS_access
                                                  SYS_access,
#endif
#ifdef SYS_chdir
                                                  SYS_chdir,
#endif
#ifdef SYS_chmod
                                                  SYS_chmod,
#endif
#ifdef SYS_chown
                                                  SYS_chown,
#endif
#ifdef SYS_close
                                                  SYS_close,
#endif
#ifdef SYS_creat
                                                  SYS_creat,
#endif
#ifdef SYS_dup
                                                  SYS_dup,
#endif
#ifdef SYS_dup2
                                                  SYS_dup2,
#endif
#ifdef SYS_dup3
                                                  SYS_dup3,
#endif
#ifdef SYS_execve
                                                  SYS_execve,
#endif
#ifdef SYS_exit
                                                  SYS_exit,
#endif
#ifdef SYS_exit_group
                                                  SYS_exit_group,
#endif
#ifdef SYS_faccessat
                                                  SYS_faccessat,
#endif
#ifdef SYS_faccessat2
                                                  SYS_faccessat2,
#endif
#ifdef SYS_fcntl
                                                  SYS_fcntl,
#endif
#ifdef SYS_fgetxattr
                                                  SYS_fgetxattr,
#endif
#ifdef SYS_flistxattr
                                                  SYS_flistxattr,
#endif
#ifdef SYS_fork
                                                  SYS_fork,
#endif
#ifdef SYS_fstat
                                                  SYS_fstat,
#endif
#ifdef SYS_fstatfs
                                                  SYS_fstatfs,
#endif
#ifdef SYS_getcwd
                                                  SYS_getcwd,
#endif
#ifdef SYS_getdents
                                                  SYS_getdents,
#endif
#ifdef SYS_getdents64
                                                  SYS_getdents64,
#endif
#ifdef SYS_getxattr
                                                  SYS_getxattr,
#endif
#ifdef SYS_ioctl
                                                  SYS_ioctl,
#endif
#ifdef SYS_lgetxattr
                                                  SYS_lgetxattr,
#endif
#ifdef SYS_lseek
                                                  SYS_lseek,
#endif
#ifdef SYS_lstat
                                                  SYS_lstat,
#endif
#ifdef SYS_mkdir
                                                  SYS_mkdir,
#endif
#ifdef SYS_mkdirat
                                                  SYS_mkdirat,
#endif
#ifdef SYS_newfstatat
                                                  SYS_newfstatat,
#endif
#ifdef SYS_open
                                                  SYS_open,
#endif
#ifdef SYS_openat
                                                  SYS_openat,
#endif
#ifdef SYS_read
                                                  SYS_read,
#endif
#ifdef SYS_readv
                                                  SYS_readv,
#endif
#ifdef SYS_rename
                                                  SYS_rename,
#endif
#ifdef SYS_rmdir
                                                  SYS_rmdir,
#endif
#ifdef SYS_stat
                                                  SYS_stat,
#endif
#ifdef SYS_statx
                                                  SYS_statx,
#endif
#ifdef SYS_unlink
                                                  SYS_unlink,
#endif
#ifdef SYS_unlinkat
                                                  SYS_unlinkat,
#endif
#ifdef SYS_write
                                                  SYS_write,
#endif
#ifdef SYS_writev
                                                  SYS_writev,
#endif
                                              });

static constexpr std::array<CPHandler_t, CAPIO_NR_SYSCALLS> build_syscall_table() {
    std::array<CPHandler_t, CAPIO_NR_SYSCALLS> _syscallTable{0};

    for (int i = 0; i < CAPIO_NR_SYSCALLS; i++) {
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
#ifdef SYS_fchmod
    _syscallTable[SYS_fchmod] = fchmod_handler;
#endif
#ifdef SYS_fchmodat
    _syscallTable[SYS_fchmodat] = fchmod_handler;
#endif
#ifdef SYS_chown
    _syscallTable[SYS_chown] = fchown_handler;
#endif
#ifdef SYS_fchown
    _syscallTable[SYS_fchown] = fchown_handler;
#endif
#ifdef SYS_fchownat
    _syscallTable[SYS_fchownat] = fchown_handler;
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
#ifdef SYS_open
    _syscallTable[SYS_open] = open_handler;
#endif
#ifdef SYS_openat
    _syscallTable[SYS_openat] = openat_handler;
#endif
#ifdef SYS_read
    _syscallTable[SYS_read] = read_handler;
#endif
#ifdef SYS_readv
    _syscallTable[SYS_readv] = readv_handler;
#endif
#ifdef SYS_rename
    _syscallTable[SYS_rename] = rename_handler;
#endif
#ifdef SYS_rmdir
    _syscallTable[SYS_rmdir] = rmdir_handler;
#endif
#ifdef SYS_stat
    _syscallTable[SYS_stat] = stat_handler;
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

    return _syscallTable;
}

static int hook(long syscall_number, long arg0, long arg1, long arg2, long arg3, long arg4,
                long arg5, long *result) {
    static constexpr std::array<CPHandler_t, CAPIO_NR_SYSCALLS> syscallTable =
        build_syscall_table();
    static const char *capio_dir = std::getenv("CAPIO_DIR");

#ifdef SYS_futex
    /**
     * Old glibc versions call the SYS_futex syscall when accessing a thread_local
     * variable. This behaviour causes an infinite recursion when checking the
     * syscall_no_intercept_flag.
     */
    if (syscall_number == SYS_futex) {
        return 1;
    }
#endif

    // If the flag is set to true, CAPIO will not
    // intercept the system calls
    if (syscall_no_intercept_flag) {
        return 1;
    }

#ifdef CAPIO_LOG
    CAPIO_LOG_LEVEL = get_capio_log_level();
#endif

    START_LOG(syscall_no_intercept(SYS_gettid), "call(syscall_number=%ld)", syscall_number);

    // If the syscall_number is higher than the maximum
    // syscall captured by CAPIO, simply return
    if (syscall_number >= CAPIO_NR_SYSCALLS) {
        return 1;
    }

    // If CAPIO_DIR is not set as environment variable,
    // then capio will not intercept the system calls
    if (capio_dir == nullptr) {
        LOG("CAPIO_DIR env var not set. returning control to kernel");
        return 1;
    }

    LOG("Handling syscall NO %ld (max num is %ld)", syscall_number, CAPIO_NR_SYSCALLS);
    return syscallTable[syscall_number](arg0, arg1, arg2, arg3, arg4, arg5, result);
}

static __attribute__((constructor)) void init() {
    init_client();
    init_filesystem();
    init_threading_support();

    long tid = syscall_no_intercept(SYS_gettid);

    int *fd_shm = get_fd_snapshot(tid);
    if (fd_shm != nullptr) {
        initialize_from_snapshot(fd_shm, tid);
    }

    init_process(tid);
    register_capio_tid(tid);

    // TODO: use var to set cache size
    init_caches();

    intercept_hook_point_clone_child  = hook_clone_child;
    intercept_hook_point_clone_parent = hook_clone_parent;
    intercept_hook_point              = hook;
    START_SYSCALL_LOGGING();
}
