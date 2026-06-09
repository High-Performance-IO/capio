#ifndef CAPIO_COMMON_ENV_HPP
#define CAPIO_COMMON_ENV_HPP

#include <charconv>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>

#include "common/constants.hpp"
#include "common/syscall.hpp"

#include <calf/StdOutLogger.h>

#ifdef __CAPIO_POSIX
#include "calf/SyscallLogger.h"
#else
#include "calf/StlLogger.h"
#endif

// TODO: remove forward declaration of function by splitting into header and impl. capio/common
inline bool is_forbidden_path(const std::string_view &path);

inline const std::filesystem::path &get_capio_dir() {
    static std::filesystem::path capio_dir{};
    START_LOG(capio_syscall(SYS_gettid), "call()");
    // TODO: if CAPIO_DIR is not set, it should be left as null

    if (capio_dir.empty()) {
        const char *val = std::getenv("CAPIO_DIR");
        auto buf        = std::unique_ptr<char[]>(new char[PATH_MAX]);

        if (val == nullptr) {

#ifndef __CAPIO_POSIX
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "CAPIO_DIR not provided");
#endif
            ERR_EXIT("Fatal:  CAPIO_DIR not provided!");

        } else {

            const char *realpath_res = capio_realpath(val, buf.get());
            if (realpath_res == nullptr) {
#ifndef __CAPIO_POSIX
                CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                                 "Fatal: CAPIO_DIR set, but folder does not exists on filesystem!");
#endif
                ERR_EXIT("error CAPIO_DIR: directory %s does not "
                         "exist. [buf=%s]",
                         val, buf.get());
            }
        }
        capio_dir = std::filesystem::path(buf.get());

        if (is_forbidden_path(capio_dir.c_str())) {
            ERR_EXIT("CAPIO_DIR %s is in forbidden path", capio_dir.string().c_str());
        }
    }
    LOG("CAPIO_DIR=%s", capio_dir.c_str());

    return capio_dir;
}

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
        } else {
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
