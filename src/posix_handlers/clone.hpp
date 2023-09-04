#ifndef CAPIO_CLONE_HPP
#define CAPIO_CLONE_HPP

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

pid_t capio_clone(int flags) {

    if (sem_wait(sem_clone) == -1)
        err_exit("sem_wait sem_clone in capio_clone", "capio_clone");

    CAPIO_DBG("clone captured flags %d\n", flags);

    pid_t pid;

    //thread creation
    if ((flags & CLONE_THREAD) == CLONE_THREAD) {
        CAPIO_DBG("thread creation\n");

        pid = 1;

        parent_tid = syscall_no_intercept(SYS_gettid);
        thread_created = true;


        CAPIO_DBG("sem_family %ld\nthread creation ending\n", sem_family);


        (*stat_enabled)[parent_tid] = false;

        CAPIO_DBG("first call size %ld\n", first_call->size());

        if (sem_wait(sem_first_call) == -1)
            err_exit("sem_wait in sem_first_call in capio_clone", "capio_clone");

        first_call->erase(syscall_no_intercept(SYS_gettid));
        if (sem_post(sem_first_call) == -1)
            err_exit("sem_post in sem_first_call in capio_clone", "capio_clone");

        CAPIO_DBG("first call after size %ld\n", first_call->size());

        CAPIO_DBG("thread creation ending 2\n");


    } else {
        //process creation

        CAPIO_DBG("process creation\n");

        fork_enabled = false;
        parent_tid = syscall_no_intercept(SYS_gettid); //now syscall_no_intercept(SYS_gettid) is the copy of the father
        pid = fork();
        if (pid == 0) { //child
            mtrace_init();
            copy_parent_files();
            if (sem_family == nullptr) {
                sem_family = sem_open(("capio_sem_family_" + std::to_string(parent_tid)).c_str(), O_CREAT | O_RDWR,
                                      S_IRUSR | S_IWUSR, 0);
                if (sem_family == SEM_FAILED)
                    err_exit("sem_open 2 sem_family in capio_clone", "capio_clone");
            }

            if (sem_post(sem_family) == -1) {
                err_exit("sem_post sem_family in capio_clone", "capio_clone");
            }

            CAPIO_DBG("returning from clone\n");

            fork_enabled = true;
            return 0;
        }

        CAPIO_DBG("father before wait\n");

        if (sem_family == nullptr) {
            sem_family = sem_open(("capio_sem_family_" + std::to_string(syscall_no_intercept(SYS_gettid))).c_str(),
                                  O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);

            if (sem_family == SEM_FAILED)
                err_exit("sem_open 3 sem_family in capio_clone", "capio_clone");
        }
        if (sem_wait(sem_family) == -1)
            err_exit("sem_wait sem_family in capio_clone", "capio_clone");

        CAPIO_DBG("father returning from clone\n");

        fork_enabled = true;
    }
    return pid;
}

int clone_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long my_tid){

    int res;
    if (fork_enabled) {
        res = capio_clone(static_cast<int>(arg0));
        if (res == 1)
            return 1;
        if (res != -2) {
            *result = (res < 0 ? -errno : res);
            return 0;
        }
    }
    return 1;
}


#endif //CAPIO_CLONE_HPP
