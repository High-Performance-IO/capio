#ifndef CAPIO_COMMON_HPP_
#define CAPIO_COMMON_HPP_

#include <iostream>
#include <string>
#include <cstring>
#include <climits>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_set>
#include <unordered_map>
#include <atomic>
#include <stdarg.h>

#include <libsyscall_intercept_hook_point.h>
#include <syscall.h>

#include "debug.hpp"
#include "data_types.hpp"
#include "filesys_utils.hpp"
#include "shared_mem.hpp"
#include "snapshots.hpp"
#include "misc.hpp"

#endif
