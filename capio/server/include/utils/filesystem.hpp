#ifndef CAPIO_SERVER_UTILS_FILESYSTEM_HPP
#define CAPIO_SERVER_UTILS_FILESYSTEM_HPP

#include <filesystem>
#include <unistd.h>

#include "calf/StlLogger.h"
#include "common/path.hpp"

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

#endif // CAPIO_SERVER_UTILS_FILESYSTEM_HPP
