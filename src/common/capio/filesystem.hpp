#ifndef CAPIO_COMMON_FILESYSTEM_HPP
#define CAPIO_COMMON_FILESYSTEM_HPP

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <sys/stat.h>

#include "env.hpp"
#include "logger.hpp"
#include "syscall.hpp"

std::string get_parent_dir_path(const std::string &file_path) {
    START_LOG(capio_syscall(SYS_gettid), "call(file-Path=%s)", file_path.c_str());
    std::size_t i = file_path.rfind('/');
    if (i == std::string::npos) {
        LOG("invalid file_path in get_parent_dir_path");
    }
    return file_path.substr(0, i);
}

inline bool in_dir(const std::string &path, const std::string &glob) {
    size_t res = path.find('/', glob.length() - 1);
    return res != std::string::npos;
}

inline bool is_absolute(const std::string *pathname) {
    return pathname != nullptr && (pathname->rfind("/", 0) == 0);
}

inline bool is_directory(const std::string &path) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s)", path.c_str());

    struct stat statbuf {};
    if (stat(path.c_str(), &statbuf) != 0) {
        LOG("Error at is_directory(path=%d) -> %d: %d (%s)", path.c_str(), errno,
            std::strerror(errno));
        return -1;
    }
    return S_ISDIR(statbuf.st_mode) == 1;
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

inline bool is_prefix(const std::string &path_1, const std::string &path_2) {
    auto res = std::mismatch(path_1.begin(), path_1.end(), path_2.begin());
    return res.first == path_2.end();
}

static inline bool is_capio_dir(const std::string &path_to_check) {
    START_LOG(capio_syscall(SYS_gettid), "call(path_to_check=%s)", path_to_check.c_str());

    const std::filesystem::path capio_dir(*get_capio_dir());
    auto res = capio_dir.compare(path_to_check) == 0;
    LOG("is_capio_dir:%s", res ? "yes" : "no");
    return res;
}

static inline bool is_capio_path(const std::string &path_to_check) {
    START_LOG(capio_syscall(SYS_gettid), "call(path_to_check=%s)", path_to_check.c_str());

    // check if path_to_check begins with CAPIO_DIR
    auto res = path_to_check.find(*get_capio_dir()) == 0;
    LOG("is_capio_path:%s", res ? "yes" : "no");
    return res;
}

#endif // CAPIO_COMMON_FILESYSTEM_HPP
