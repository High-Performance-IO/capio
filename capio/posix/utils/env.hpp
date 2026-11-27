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

/**
 * @brief The CAPIO_DIR of this process, read once: the mount point of the virtual file system that
 * the server sees, whose "/" it is. Empty if CAPIO_DIR is not set: then no path is CAPIO's, and the
 * library leaves every system call to the kernel.
 *
 * The memory variant does not need the directory to exist: if it does, it is resolved like the
 * paths the process gets from the kernel, otherwise it is only made absolute. The FS variant keeps
 * its files there, so the directory must exist.
 */
inline const std::filesystem::path &get_capio_dir() {
    static const std::filesystem::path capio_dir = [] {
        const char *value = std::getenv("CAPIO_DIR");
        if (value == nullptr) {
            return std::filesystem::path{};
        }
        const auto buf = std::unique_ptr<char[]>(new char[PATH_MAX]);
        std::filesystem::path dir;
        if (capio_realpath(value, buf.get()) != nullptr) {
            dir = buf.get();
        } else {
            FS_MODE({
                START_LOG(capio_syscall(SYS_gettid), "call()");
                ERR_EXIT("CAPIO_DIR %s does not exist: file system streaming needs it", value);
            })
            dir = std::filesystem::absolute(value).lexically_normal();
            if (!dir.has_filename() && dir != dir.root_path()) {
                dir = dir.parent_path(); // no trailing slash
            }
        }
        if (is_forbidden_path(dir.c_str())) {
            START_LOG(capio_syscall(SYS_gettid), "call()");
            ERR_EXIT("CAPIO_DIR %s is in forbidden path", dir.c_str());
        }
        return dir;
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
