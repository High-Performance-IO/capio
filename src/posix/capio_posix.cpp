/**
 * todo: in the future this should not be required. this constant is used to tell some of the shared posix_utils hot to behave.
 * mainly it is used to distinguish wheter to use libsyscall_intercept or not
 */
#define _COMPILE_CAPIO_POSIX

#include <array>
#include <unordered_map>

#include <asm-generic/unistd.h>
#include <libsyscall_intercept_hook_point.h>
#include <syscall.h>

#include "capio/errors.hpp"

#include "globals.hpp"
#include "handlers.hpp"

/**
 * Handler for syscall not handled and interrupt syscall_intercept
 */
static int not_handled_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result, long tid) {
  return 1;
}

/**
 * Handler for syscall handled, but not yet implemented and interrupt syscall_intercept
 */
static int not_implemented_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result, long tid) {
  errno = ENOTSUP;
  *result = -errno;
  return 0;
}

static constexpr std::array<CPHandler_t, __NR_syscalls> buildSyscallTable() {
    std::array<CPHandler_t, __NR_syscalls> _syscallTable{0};

    for (int i = 0; i < __NR_syscalls; i++)
        _syscallTable[i] = not_handled_handler;

    _syscallTable[SYS_access] = access_handler;
    _syscallTable[SYS_chdir] = chdir_handler;
    _syscallTable[SYS_chmod] = fchmod_handler;
    _syscallTable[SYS_chown] = fchown_handler;
    _syscallTable[SYS_clone] = clone_handler;
    _syscallTable[SYS_close] = close_handler;
    _syscallTable[SYS_creat] = creat_handler;
    _syscallTable[SYS_dup] = dup_handler;
    _syscallTable[SYS_dup2] = dup2_handler;
    _syscallTable[SYS_execve] = execve_handler;
    _syscallTable[SYS_exit] = exit_handler;
    _syscallTable[SYS_exit_group] = exit_handler;
    _syscallTable[SYS_faccessat] = faccessat_handler;
    _syscallTable[SYS_fcntl] = fcntl_handler;
    _syscallTable[SYS_fgetxattr] = fgetxattr_handler;
    _syscallTable[SYS_flistxattr] = not_implemented_handler;
    _syscallTable[SYS_fork] = fork_handler;
    _syscallTable[SYS_fstat] = fstat_handler;
    _syscallTable[SYS_fstatfs] = fstatfs_handler;
    _syscallTable[SYS_getcwd] = getcwd_handler;
    _syscallTable[SYS_getdents] = getdents_handler;
    _syscallTable[SYS_getdents64] = getdents64_handler;
    _syscallTable[SYS_getxattr] = not_implemented_handler;
    _syscallTable[SYS_ioctl] = ioctl_handler;
    _syscallTable[SYS_lgetxattr] = not_implemented_handler;
    _syscallTable[SYS_lseek] = lseek_handler;
    _syscallTable[SYS_lstat] = lstat_handler;
    _syscallTable[SYS_mkdir] = mkdir_handler;
    _syscallTable[SYS_mkdirat] = mkdirat_handler;
    _syscallTable[SYS_newfstatat] = fstatat_handler;
    _syscallTable[SYS_openat] = openat_handler;
    _syscallTable[SYS_read] = read_handler;
    _syscallTable[SYS_rename] = rename_handler;
    _syscallTable[SYS_stat] = lstat_handler;
    _syscallTable[SYS_unlink] = unlink_handler;
    _syscallTable[SYS_unlinkat] = unlinkat_handler;
    _syscallTable[SYS_write] = write_handler;
    _syscallTable[SYS_writev] = writev_handler;

    return _syscallTable;
}

static int hook(long syscall_number, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {

    static const std::array<CPHandler_t, __NR_syscalls> syscallTable = buildSyscallTable();

    long int tid = syscall_no_intercept(SYS_gettid);

    if (stat_enabled == nullptr)
        stat_enabled = new std::unordered_map<long int, bool>;


    auto it = stat_enabled->find(tid);
    if (it != stat_enabled->end())
        if (!(it->second))
            return 1;

#ifdef CAPIOLOG
      CPHandler_t callback = syscallTable[syscall_number];

      if(callback == not_implemented_handler) {
            CAPIO_DBG("capio_posix TID[%ld]: syscall number %ld is not implemented\n",
                      tid, syscall_number);
      }
      if(callback == not_handled_handler) {
            CAPIO_DBG("capio_posix TID[%ld]: syscall number %ld is not handled\n", tid, syscall_number);
      }
      return callback(arg0, arg1, arg2, arg3, arg4, arg5, result, tid);
#else
    return syscallTable[syscall_number](arg0, arg1, arg2, arg3, arg4, arg5, result, tid);
#endif
}

static void hook_clone_parent(long parent_tid) {
    auto child_tid = static_cast<pid_t>(syscall_no_intercept(SYS_gettid));

    CAPIO_DBG("hook_clone_parent PARENT_TID[%ld] CHILD_TID[%ld]: delegate to mtrace_init\n", parent_tid, child_tid);

    mtrace_init(child_tid);

    CAPIO_DBG("hook_clone_parent PARENT_TID[%ld] CHILD_TID[%ld]: add clone_request\n", parent_tid, child_tid);

    clone_request(parent_tid, child_tid);

    CAPIO_DBG("hook_clone_parent PARENT_TID[%ld] CHILD_TID[%ld]: clone_request added, return\n", parent_tid, child_tid);
}

static __attribute__((constructor)) void
init() {
    init_client();
    mtrace_init(gettid());

    intercept_hook_point = hook;
    intercept_hook_point_clone_parent = hook_clone_parent;
}