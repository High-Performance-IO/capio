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

#endif // CAPIO_POSIX_UTILS_ENV_HPP
