#ifndef CAPIO_COMMON_FILESYSTEM_HPP
#define CAPIO_COMMON_FILESYSTEM_HPP

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

#include <sys/stat.h>

#ifdef __CAPIO_POSIX
#include "calf/SyscallLogger.h"
#else
#include "calf/StlLogger.h"
#endif

#include "common/syscall.hpp"
#include "env.hpp"

constexpr std::array CAPIO_DIR_FORBIDDEN_PATHS = {std::string_view{"/proc/"},
                                                  std::string_view{"/sys/"}};

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

inline bool in_dir(const std::string &path, const std::string &glob) {
    const size_t res = path.find('/', glob.length() - 1);
    return res != std::string::npos;
}

inline bool is_directory(int dirfd) {
    START_LOG(capio_syscall(SYS_gettid), "call(dirfd=%d)", dirfd);

    struct stat path_stat{};
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

/**
 * Resolve a possible symbolic link to the absolute path that it points to
 * @param input_path
 * @return
 */
[[maybe_unused]] [[nodiscard]] static std::string
resolve_possible_symlink(const std::filesystem::path &input_path) {

    // Cache for resolved symbolic links: link -> realpath
    static std::unordered_map<std::string, std::string> resolved_symlinks_cache;

    if (resolved_symlinks_cache.find(input_path) == resolved_symlinks_cache.end()) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s)", input_path.c_str());

        LOG("Absolute path = %s", input_path.c_str());

#ifdef __CAPIO_POSIX
        syscall_no_intercept_flag = true;
#endif

        std::filesystem::path resolved;
        std::filesystem::path input_abs_path = std::filesystem::absolute(input_path);
        for (const auto &part : input_abs_path) {
            resolved /= part;

            if (part == "." || part.empty()) {
                continue;
            }
            if (part == "..") {
                resolved = resolved.parent_path();
                continue;
            }
            if (std::filesystem::is_symlink(resolved)) {
                char buf[PATH_MAX]{0};
                const auto result =
                    capio_syscall(SYS_readlinkat, AT_FDCWD, resolved.c_str(), buf, sizeof(buf) - 1);
                if (result == -1) {
                    LOG("File might not exist. path was %s and  Error is %s", resolved.c_str(),
                        strerror(errno));
                    continue;
                }

                if (std::filesystem::path target(buf); target.is_relative()) {
                    resolved = resolved.parent_path() / target;
                } else {
                    resolved = target;
                }
            }
        }
#ifdef __CAPIO_POSIX
        syscall_no_intercept_flag = false;
#endif

        resolved_symlinks_cache[input_path] = resolved;
    }
    return resolved_symlinks_cache[input_path];
}

#endif // CAPIO_COMMON_FILESYSTEM_HPP
