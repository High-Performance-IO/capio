/**
 * todo: in the future this should not be required. this constant is used to tell some of the shared posix_utils hot to behave.
 * mainly it is used to distinguish wheter to use libsyscall_intercept or not
 */
#define _COMPILE_CAPIO_POSIX


#include <filesystem>
#include <asm-generic/unistd.h>
#include <sys/statfs.h>
#include <libsyscall_intercept_hook_point.h>
#include <array>

#include "common_utils/include.hpp"
#include "data_structure/include.hpp"
#include "posix_utils/common.hpp"


std::string *capio_dir = nullptr;

std::string *capio_app_name = nullptr;
std::set<int> *first_call = nullptr;

std::string *current_dir = nullptr;

int num_writes_batch = 1;

int actual_num_writes = 1;
long int parent_tid = 0;
sem_t *sem_family = nullptr;

sem_t *sem_first_call = nullptr;
sem_t *sem_clone = nullptr;
sem_t *sem_tmp = nullptr;
CPSemsWrite_t *sems_write = nullptr;

CPFileDescriptors_t *capio_files_descriptors = nullptr;
CPFilesPaths_t *capio_files_paths = nullptr;
CPFiles_t *files = nullptr;
CPBufRequest_t *buf_requests = nullptr;
CPBufResponse_t *bufs_response = nullptr;
CPStatEnabled_t *stat_enabled = nullptr; //TODO: protect with a semaphore
CPThreadDataBufs_t *threads_data_bufs = nullptr;
bool dup2_enabled = true;
bool fork_enabled = true;
bool thread_created = false;

#include "posix_handlers/handlers.hpp"

constexpr std::array<CPHandler_t, __NR_syscalls> buildSyscallTable() {
    std::array<CPHandler_t, __NR_syscalls> _syscallTable{0};

    for (int i = 0; i < __NR_syscalls; i++)
        _syscallTable[i] = not_handled_handler;

    _syscallTable[SYS_write] = write_handler;
    _syscallTable[SYS_openat] = openat_handler;
    _syscallTable[SYS_write] = write_handler;
    _syscallTable[SYS_read] = read_handler;
    _syscallTable[SYS_close] = close_handler;
    _syscallTable[SYS_lseek] = lseek_handler;
    _syscallTable[SYS_lseek] = lseek_handler;
    _syscallTable[SYS_writev] = writev_handler;
    _syscallTable[SYS_fcntl] = fcntl_handler;
    _syscallTable[SYS_lgetxattr] = not_implemented_handler;
    _syscallTable[SYS_getxattr] = not_implemented_handler;
    _syscallTable[SYS_fgetxattr] = fgetxattr_handler;
    _syscallTable[SYS_flistxattr] = flistxattr_handler;
    _syscallTable[SYS_ioctl] = ioctl_handler;
    _syscallTable[SYS_exit] = exit_handler;
    _syscallTable[SYS_exit_group] = exit_handler;
    _syscallTable[SYS_lstat] = lstat_handler;
    _syscallTable[SYS_stat] = lstat_handler;
    _syscallTable[SYS_fstat] = fstat_handler;
    _syscallTable[SYS_newfstatat] = fstatat_handler;
    _syscallTable[SYS_creat] = creat_handler;
    _syscallTable[SYS_access] = access_handler;
    _syscallTable[SYS_faccessat] = faccessat_handler;
    _syscallTable[SYS_unlink] = unlink_handler;
    _syscallTable[SYS_unlinkat] = unlinkat_handler;
    _syscallTable[SYS_chown] = fchown_handler;
    _syscallTable[SYS_chmod] = fchmod_handler;
    _syscallTable[SYS_dup] = dup_handler;
    _syscallTable[SYS_dup2] = dup2_handler;
    _syscallTable[SYS_fork] = fork_handler;
    _syscallTable[SYS_clone] = clone_handler;
    _syscallTable[SYS_mkdir] = mkdir_handler;
    _syscallTable[SYS_mkdirat] = mkdirat_handler;
    _syscallTable[SYS_getdents] = getdents_handler;
    _syscallTable[SYS_getdents64] = getdents64_handler;
    _syscallTable[SYS_chdir] = chdir_handler;
    _syscallTable[SYS_rename] = rename_handler;
    _syscallTable[SYS_fstatfs] = fstatfs_handler;
    _syscallTable[SYS_getcwd] = getcw_handler;
    _syscallTable[SYS_execve] = execve_handler;


    return _syscallTable;
}

std::array<CPHandler_t, __NR_syscalls> syscallTable = buildSyscallTable();

static int hook(long syscall_number, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {

    long int my_tid = syscall_no_intercept(SYS_gettid);

    if (stat_enabled == nullptr)
        stat_enabled = new std::unordered_map<long int, bool>;


    auto it = stat_enabled->find(my_tid);
    if (it != stat_enabled->end())
        if (!(it->second))
            return 1;


    if (sem_first_call == nullptr) {
        sem_first_call = new sem_t;
        if (sem_init(sem_first_call, 0, 1) == -1) {
            err_exit("sem_init sem_first_call in hook", "hook");
        }
        sem_clone = new sem_t;
        if (sem_init(sem_clone, 0, 1) == -1) {
            err_exit("sem_init sem_clone in hook", "hook");
        }
    }


    if (sem_wait(sem_first_call) == -1)
        err_exit("sem_wait sem_first_call in hook", "hook");


    if (first_call == nullptr || first_call->find(my_tid) == first_call->end())
        mtrace_init();
    else
        if (sem_post(sem_first_call) == -1)
            err_exit("sem_post sem_first_call in hook", "hook");

#ifdef CAPIOLOG
      CAPIO_DBG("Currently handling syscall number: %ld\n", syscall_number);

      CPHandler_t callback = syscallTable[syscall_number];

      if(callback == not_implemented_handler)
          CAPIO_DBG("INFO: callback is not_implemented_handler");
      if(callback == not_handled_handler)
            CAPIO_DBG("WARNING: callback is not_handled_handler");
      return callback(arg0, arg1, arg2, arg3, arg4, arg5, result, my_tid);
#else
    return syscallTable[syscall_number](arg0, arg1, arg2, arg3, arg4, arg5, result, my_tid);
#endif
}

static __attribute__((constructor)) void
init(void) {
    intercept_hook_point = hook;
}