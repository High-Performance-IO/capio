//
// Created by marco on 05/09/23.
//

#ifndef CAPIO_HANDLERS_HPP
#define CAPIO_HANDLERS_HPP

#include "common.hpp"
#include "lseek.hpp"
#include "write.hpp"
#include "openat.hpp"
#include "close.hpp"
#include "read.hpp"
#include "writev.hpp"
#include "fnctl.hpp"
#include "fgetxattr.hpp"
#include "ioctl.hpp"
#include "exit_group.hpp"
#include "lstat.hpp"
#include "fstat.hpp"
#include "fstatat.hpp"
#include "creat.hpp"
#include "access.hpp"
#include "faccessat.hpp"
#include "unlink.hpp"
#include "unlinkat.hpp"
#include "fchown.hpp"
#include "fchmod.hpp"
#include "dup.hpp"
#include "dup2.hpp"
#include "fork.hpp"
#include "clone.hpp"
#include "mkdir.hpp"
#include "mkldirat.hpp"
#include "getdents.hpp"
#include "chdir.hpp"
#include "rename.hpp"
#include "fstatfs.hpp"
#include "getcwd.hpp"
#include "flistxattr.hpp"
#include "execve.hpp"


//default handlers

/**
 * Handler for syscall not handled and interrupt syscall_intercept
 */
int not_handled_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result, long my_tid) {
    return 1;
}

/**
 * Handler for syscall handled, but not yet implemented and interrupt syscall_intercept
 */
int not_implemented_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result, long my_tid) {
    errno = ENODATA;
    *result = -errno;
    return 0;
}

#endif //CAPIO_HANDLERS_HPP
