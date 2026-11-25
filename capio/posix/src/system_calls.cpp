#include "utils/system_calls.hpp"

#include <libsyscall_intercept_hook_point.h>
#include <sys/syscall.h>

long SystemCalls::close(const int fd) { return syscall_no_intercept(SYS_close, fd); }
