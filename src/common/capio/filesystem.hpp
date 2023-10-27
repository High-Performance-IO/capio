#ifndef CAPIO_COMMON_FILESYSTEM_HPP
#define CAPIO_COMMON_FILESYSTEM_HPP

#include <sys/stat.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>

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

bool is_prefix(std::string path_1, std::string path_2) {
    auto res = std::mismatch(path_1.begin(), path_1.end(), path_2.begin());
    return res.first == path_2.end();
}

static inline bool is_capio_path(long tid, const std::string &path_to_check,
                                 const std::string &capio_dir) {
    START_LOG(tid, "call(%s, %s)", path_to_check.c_str(), capio_dir.c_str());

    return (std::mismatch(capio_dir.begin(), capio_dir.end(), path_to_check.begin()).first ==
                capio_dir.end() &&
            capio_dir.size() != path_to_check.size());
}

const std::string *capio_posix_realpath(long tid, const std::string *pathname,
                                        const std::string *capio_dir,
                                        const std::string *current_dir) {
    START_LOG(tid, "call(path=%s, capio_dir=%s, current_dir=%s)", pathname->c_str(),
              capio_dir->c_str(), current_dir->c_str());
    char *posix_real_path = capio_realpath((char *) pathname->c_str(), nullptr);

    // if capio_realpath fails, then it should be a capio_file
    if (posix_real_path == nullptr) {
        LOG("path is null due to errno='%s'", strerror(errno));

        if (current_dir->find(*capio_dir) != std::string::npos) {
            if (pathname[0] != "/") {
                auto newPath = new std::string(*capio_dir + "/" + *pathname);

                // remove /./ from path
                std::size_t pos = 0;
                while ((pos = newPath->find("/./", pos)) != std::string::npos) {
                    newPath->replace(newPath->find("/./"), 3, "/");
                    pos += 1;
                }

                LOG("Computed absolute path = %s", newPath->c_str());
                return newPath;
            } else {
                LOG("Path=%s is already absolute", pathname->c_str());
            }
            return pathname;
        } else {
            // if file not found, then error is returned
            LOG("Fatal: file %s is not a posix file, nor a capio "
                "file!",
                pathname->c_str());
            exit(EXIT_FAILURE);
        }
    }

    // if not, then check for realpath trough libc implementation
    LOG("Computed realpath = %s", posix_real_path);
    return new std::string(posix_real_path);
}

#endif // CAPIO_COMMON_FILESYSTEM_HPP
