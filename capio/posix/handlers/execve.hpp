#ifndef CAPIO_POSIX_HANDLERS_EXECVE_HPP
#define CAPIO_POSIX_HANDLERS_EXECVE_HPP

#if defined(SYS_execve)

#include "utils/filesystem.hpp"
#include "utils/tid.hpp"

int execve_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    START_LOG(capio_tid(), "call()");

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
    fd_table->createSnapshot(capio_tid());

    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // SYS_execve
#endif // CAPIO_POSIX_HANDLERS_EXECVE_HPP
