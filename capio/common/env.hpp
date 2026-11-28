#ifndef CAPIO_COMMON_ENV_HPP
#define CAPIO_COMMON_ENV_HPP

#include <charconv>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>

#include "common/constants.hpp"
#include "common/path.hpp"
#include "common/syscall.hpp"

#include <calf/StdOutLogger.h>

#ifdef __CAPIO_POSIX
#include "calf/SyscallLogger.h"
#else
#include "calf/StlLogger.h"
#endif

inline long get_cache_lines() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    static long data_bufs_size = -1;
    if (data_bufs_size == -1) {
        LOG("Value not set. getting value");
        char *value = std::getenv("CAPIO_CACHE_LINES");
        if (value != nullptr) {
            LOG("Getting value from environment variable");
            data_bufs_size = strtol(value, nullptr, 10);
        } else {
            LOG("Getting default value");
            data_bufs_size = CAPIO_CACHE_LINES_DEFAULT;
        }
    }
    LOG("data_bufs_size=%ld", data_bufs_size);
    return data_bufs_size;
}

inline long get_cache_line_size() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    static long data_bufs_count = -1;
    if (data_bufs_count == -1) {
        LOG("Value not set. getting value");
        char *value = std::getenv("CAPIO_CACHE_LINE_SIZE");
        if (value != nullptr) {
            LOG("Getting value from environment variable");
            data_bufs_count = strtol(value, nullptr, 10);
        }
        // a value that is not a positive number would give cache lines that hold nothing
        if (data_bufs_count <= 0) {
            LOG("Getting default value");
            data_bufs_count = CAPIO_CACHE_LINE_SIZE_DEFAULT;
        }
    }
    LOG("data_bufs_count=%ld", data_bufs_count);
    return data_bufs_count;
}

inline std::string get_capio_workflow_name() {
    static std::string name;
    if (name.empty()) {
        auto tmp = std::getenv("CAPIO_WORKFLOW_NAME");
        if (tmp != nullptr) {
            name = tmp;
        } else {
            name = CAPIO_DEFAULT_WORKFLOW_NAME;
        }
    }
    return name;
}

#endif // CAPIO_COMMON_ENV_HPP
