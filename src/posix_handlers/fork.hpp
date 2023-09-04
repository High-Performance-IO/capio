//
// Created by marco
#ifndef CAPIO_FORK_HPP
#define CAPIO_FORK_HPP

pid_t capio_fork() {
    fork_enabled = false;
#ifdef CAPIOLOG
    CAPIO_DBG("fork captured\n");
#endif

    //pid_t pid = fork();
    pid_t pid = syscall_no_intercept(SYS_fork);
    if (pid == 0) { //child
        parent_tid = syscall_no_intercept(SYS_gettid); //now syscall_no_intercept(SYS_gettid) is the copy of the father
        mtrace_init();
        copy_parent_files();
        return 0;
    }
    fork_enabled = true;
    return pid;

}

int fork_handler(long arg0, long arg1, long arg2,
                  long arg3, long arg4, long arg5, long* result,
                  long my_tid){

int res;
#ifdef CAPIOLOG
    CAPIO_DBG("fork before captured\n");
#endif
    if (fork_enabled) {
        res = capio_fork();
        *result = (res < 0 ? -errno : res);
        return 0;
    } else
        res = -2;
    return 1;
}

#endif //CAPIO_FORK_HPP
