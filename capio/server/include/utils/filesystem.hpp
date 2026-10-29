#ifndef CAPIO_SERVER_UTILS_FILESYSTEM_HPP
#define CAPIO_SERVER_UTILS_FILESYSTEM_HPP

#include <filesystem>
#include <unistd.h>

#include "calf/StlLogger.h"

#include "utils/runtime_configuration.hpp"

inline std::filesystem::path get_parent_dir_path(const std::filesystem::path &file_path) {
    START_LOG(gettid(), "call(file_path=%s)", file_path.c_str());
    if (file_path == file_path.root_path()) {
        return file_path;
    }
    const size_t pos = file_path.native().rfind('/');
    if (pos == std::string::npos) {
        LOG("invalid file_path in get_parent_dir_path");
    }
    return {file_path.native().substr(0, pos)};
}

/// @brief Server counterpart of is_capio_dir() in posix/utils/filesystem.hpp, which reads CAPIO_DIR
/// from the environment: the server checks against the configured capio.directory instead.
inline bool is_capio_dir(const std::filesystem::path &path_to_check) {
    START_LOG(gettid(), "call(path_to_check=%s)", path_to_check.c_str());

    const auto res = RuntimeConfiguration::get().getCapioDir().compare(path_to_check) == 0;
    LOG("is_capio_dir:%s", res ? "yes" : "no");
    return res;
}

#endif // CAPIO_SERVER_UTILS_FILESYSTEM_HPP
