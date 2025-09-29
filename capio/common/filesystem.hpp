#ifndef CAPIO_COMMON_FILESYSTEM_HPP
#define CAPIO_COMMON_FILESYSTEM_HPP

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <sys/stat.h>

#include "common/logger.hpp"
#include "common/syscall.hpp"
#include "env.hpp"

std::filesystem::path get_parent_dir_path(const std::filesystem::path &file_path) {
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

inline bool in_dir(const std::string &path, const std::string &glob) {
    const size_t res = path.find('/', glob.length() - 1);
    return res != std::string::npos;
}

inline bool is_directory(int dirfd) {
    START_LOG(capio_syscall(SYS_gettid), "call(dirfd=%d)", dirfd);

    struct stat path_stat {};
    int tmp = fstat(dirfd, &path_stat);
    if (tmp != 0) {
        LOG("Error at is_directory(dirfd=%d) -> %d: %d (%s)", dirfd, tmp, errno,
            std::strerror(errno));
        return -1;
    }
    return S_ISDIR(path_stat.st_mode) == 1;
}

inline bool is_prefix(const std::filesystem::path &path_1, const std::filesystem::path &path_2) {
    const auto relpath = path_2.lexically_relative(path_1);
    return !relpath.empty() && relpath.native().rfind("..", 0) != 0;
}

inline bool is_forbidden_path(const std::string_view &path) {
    return std::any_of(CAPIO_DIR_FORBIDDEN_PATHS.cbegin(), CAPIO_DIR_FORBIDDEN_PATHS.cend(),
                       [&path](const std::string_view &forbidden_path) {
                           return path.rfind(forbidden_path, 0) == 0;
                       });
}

inline bool is_capio_dir(const std::filesystem::path &path_to_check) {
    START_LOG(capio_syscall(SYS_gettid), "call(path_to_check=%s)", path_to_check.c_str());

    const auto res = get_capio_dir().compare(path_to_check) == 0;
    LOG("is_capio_dir:%s", res ? "yes" : "no");
    return res;
}

inline bool is_capio_path(const std::filesystem::path &path_to_check) {
    START_LOG(capio_syscall(SYS_gettid), "call(path_to_check=%s)", path_to_check.c_str());

    // check if path_to_check begins with CAPIO_DIR
    const auto res = is_prefix(get_capio_dir(), path_to_check);
    LOG("is_capio_path:%s", res ? "yes" : "no");
    return res;
}

#endif // CAPIO_COMMON_FILESYSTEM_HPP
