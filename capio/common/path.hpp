#ifndef CAPIO_COMMON_PATH_HPP
#define CAPIO_COMMON_PATH_HPP

#include <filesystem>

#ifdef __CAPIO_POSIX
#include "calf/SyscallLogger.h"
#else
#include "calf/StlLogger.h"
#endif

#include "common/syscall.hpp"

// Path helpers that do not depend on the environment, usable by both the server and posix.

inline std::filesystem::path get_parent_dir_path(const std::filesystem::path &file_path) {
    START_LOG(capio_syscall(SYS_gettid), "call(file_path=%s)", file_path.c_str());
    if (file_path == file_path.root_path()) {
        return file_path;
    }
    const size_t pos = file_path.native().rfind('/');
    if (pos == std::string::npos) {
        LOG("invalid file_path in get_parent_dir_path");
    }
    return {file_path.native().substr(0, pos)};
}

#endif // CAPIO_COMMON_PATH_HPP
