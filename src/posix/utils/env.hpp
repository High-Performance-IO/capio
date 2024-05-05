#ifndef CAPIO_POSIX_UTILS_ENV_HPP
#define CAPIO_POSIX_UTILS_ENV_HPP

#include <cstdlib>
#include <iostream>

#include "capio/logger.hpp"

const char *get_capio_app_name() {
    static char *capio_app_name = std::getenv("CAPIO_APP_NAME");

    if (capio_app_name == nullptr) {
        return "default_app";
    }
    return capio_app_name;
}

#endif // CAPIO_POSIX_UTILS_ENV_HPP
