#ifndef CAPIO_POSIX_UTILS_ENV_HPP
#define CAPIO_POSIX_UTILS_ENV_HPP

#include <cstdlib>
#include <iostream>

#include "capio/logger.hpp"

inline const char *get_capio_app_name() {
    static char *capio_app_name = std::getenv("CAPIO_APP_NAME");

    if (capio_app_name == nullptr) {
        return CAPIO_DEFAULT_APP_NAME;
    }
    return capio_app_name;
}

inline capio_off64_t get_capio_write_cache_size() {
    static char *cache_size_str = std::getenv("CAPIO_WRITER_CACHE_SIZE");

    static capio_off64_t cache_size = cache_size_str == nullptr
                                          ? CAPIO_CACHE_LINE_SIZE_DEFAULT
                                          : std::strtoull(cache_size_str, nullptr, 10);
    return cache_size;
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

inline long get_posix_read_cache_line_size() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    static long data_bufs_count = -1;
    if (data_bufs_count == -1) {
        LOG("Value not set. getting value");
        char *value = std::getenv("CAPIO_POSIX_CACHE_LINE_SIZE");
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

#endif // CAPIO_POSIX_UTILS_ENV_HPP
