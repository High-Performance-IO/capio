#ifndef CAPIO_POSIX_UTILS_FILESYSTEM_HPP
#define CAPIO_POSIX_UTILS_FILESYSTEM_HPP

#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

#include "capio/env.hpp"
#include "capio/filesystem.hpp"
#include "capio/logger.hpp"
#include "requests.hpp"
#include "types.hpp"

std::string get_capio_parent_dir(const std::string &path) {
    auto pos = path.rfind('/');
    return path.substr(0, pos);
}

std::string get_dir_path(int dirfd) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(dirfd=%d)", dirfd);

    auto it = capio_files_descriptors->find(dirfd);
    if (it != capio_files_descriptors->end()) {
        LOG("dirfd %d points to path %s", dirfd, it->second.c_str());
        return it->second;
    } else {
        char proclnk[128];
        char dir_pathname[PATH_MAX];
        sprintf(proclnk, "/proc/self/fd/%d", dirfd);
        ssize_t r = readlink(proclnk, dir_pathname, PATH_MAX);
        if (r < 0) {
            fprintf(stderr, "failed to readlink\n");
            return "";
        }
        dir_pathname[r] = '\0';
        LOG("dirfd %d points to path %s", dirfd, dir_pathname);
        return dir_pathname;
    }
}

inline blkcnt_t get_nblocks(off64_t file_size) {
    if (file_size % 4096 == 0) {
        return file_size / 512;
    }

    return file_size / 512 + 8;
}

#endif // CAPIO_POSIX_UTILS_FILESYSTEM_HPP
