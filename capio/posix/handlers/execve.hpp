#ifndef CAPIO_POSIX_HANDLERS_EXECVE_HPP
#define CAPIO_POSIX_HANDLERS_EXECVE_HPP

#if defined(SYS_execve)

#include "utils/filesystem.hpp"
#include "utils/requests.hpp"

int execve_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call()");

    FS_MODE({
        // Nothing is closed before the exec works. The snapshot tells the program after it which
        // descriptors the kernel closed (close-on-exec), and that program tells the server.
        for (const int fd : fd_table->fds()) {
            const long fd_flags = syscall_no_intercept(SYS_fcntl, fd, F_GETFD);
            if (fd_flags < 0) { // closed by a call CAPIO does not handle yet
                fd_table->remove(fd);
            } else {
                fd_table->setCloexec(fd, (fd_flags & FD_CLOEXEC) != 0);
            }
        }
    })
    fd_table->createSnapshot(tid);

    MEMORY_MODE({ return CAPIO_POSIX_SYSCALL_SKIP; })
    FS_MODE({
        // The exec kills the other threads: a request one of them is writing would be left half
        // written, and every client of the node would wait for it forever
        stop_requests_of_other_threads(tid);
        *result = syscall_no_intercept(SYS_execve, arg0, arg1, arg2);
        resume_requests(); // it failed: the other threads still run
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    })
}

#endif // SYS_execve
#endif // CAPIO_POSIX_HANDLERS_EXECVE_HPP
