#ifndef CAPIO_COMMON_ENV_HPP
#define CAPIO_COMMON_ENV_HPP

#include <charconv>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <sys/stat.h>

#include "common/syscall.hpp"
#include "logger.hpp"

inline const std::filesystem::path &get_capio_dir() {
    static std::filesystem::path capio_dir{};
    START_LOG(capio_syscall(SYS_gettid), "call()");
    // TODO: if CAPIO_DIR is not set, it should be left as null

    if (capio_dir.empty()) {
        const char *val = std::getenv("CAPIO_DIR");
        auto buf        = std::unique_ptr<char[]>(new char[PATH_MAX]);

        if (val == nullptr) {

            std::cout << "\n"
                      << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Fatal: CAPIO_DIR not provided!"
                      << std::endl;
            ERR_EXIT("Fatal:  CAPIO_DIR not provided!");

        } else {

            const char *realpath_res = capio_realpath(val, buf.get());
            if (realpath_res == nullptr) {
                std::cout << "\n"
                          << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR
                          << "Fatal: CAPIO_DIR set, but folder does not exists on filesystem!"
                          << std::endl;
                ERR_EXIT("error CAPIO_DIR: directory %s does not "
                         "exist. [buf=%s]",
                         val, buf.get());
            }
        }
        capio_dir = std::filesystem::path(buf.get());
        for (auto &forbidden_path : CAPIO_DIR_FORBIDDEN_PATHS) {
            if (capio_dir.native().rfind(forbidden_path, 0) == 0) {
                ERR_EXIT("CAPIO_DIR inside %s file system is not supported", forbidden_path.data());
            }
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

inline int get_capio_log_level() {
    static int level = -2;
    if (level == -2) {
        char *log_level = std::getenv("CAPIO_LOG_LEVEL");
        if (log_level == nullptr) {
            level = 0;
        } else {
            auto [ptr, ec] = std::from_chars(log_level, log_level + strlen(log_level), level);
            if (ec != std::errc()) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "invalid CAPIO_LOG_LEVEL value"
                          << std::endl;
                level = 0;
            }
        }
    }
    return level;
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
