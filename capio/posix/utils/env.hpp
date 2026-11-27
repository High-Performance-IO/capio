#ifndef CAPIO_POSIX_UTILS_ENV_HPP
#define CAPIO_POSIX_UTILS_ENV_HPP

#include <climits>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "calf/SyscallLogger.h"
#include "common/constants.hpp"
#include "common/path.hpp"
#include "common/syscall.hpp"

/// @brief The canonical CAPIO_DIR of this process, read once. Empty if CAPIO_DIR is not set: then
/// no path is CAPIO's, and the library leaves every system call to the kernel.
inline const std::filesystem::path &get_capio_dir() {
    static const std::filesystem::path capio_dir = [] {
        const char *value = std::getenv("CAPIO_DIR");
        if (value == nullptr) {
            return std::filesystem::path{};
        }
        const auto buf = std::unique_ptr<char[]>(new char[PATH_MAX]);
        if (capio_realpath(value, buf.get()) == nullptr) {
            ERR_EXIT("error CAPIO_DIR: directory %s does not exist", value);
        }
        if (is_forbidden_path(buf.get())) {
            ERR_EXIT("CAPIO_DIR %s is in forbidden path", buf.get());
        }
        return std::filesystem::path(buf.get());
    }();
    return capio_dir;
}

inline const char *get_capio_app_name() {
    static char *capio_app_name = std::getenv("CAPIO_APP_NAME");

    if (capio_app_name == nullptr) {
        return CAPIO_DEFAULT_APP_NAME;
    }
    return capio_app_name;
}

#endif // CAPIO_POSIX_UTILS_ENV_HPP
