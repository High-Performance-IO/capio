#ifndef POSIX_READDIR_HPP
#define POSIX_READDIR_HPP

#include <capio/logger.hpp>
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

// Map &DIR -> dirpath
std::unordered_map<unsigned long int, std::string> opened_directory;

DIR *opendir(const char *name) {
    char realpath[PATH_MAX]{0};
    capio_realpath(name, realpath);
    START_LOG(gettid(), "call(path=%s)", realpath);

    static DIR *(*real_opendir)(const char *) = NULL;

    if (!real_opendir) {
        syscall_no_intercept_flag = true;
        real_opendir              = (DIR * (*) (const char *) ) dlsym(RTLD_NEXT, "opendir");
        syscall_no_intercept_flag = false;
        if (!real_opendir) {
            ERR_EXIT("Failed to find original opendir: %s\n", dlerror());
        }
    }

    if (!is_capio_path(realpath)) {
        LOG("Not a CAPIO path. continuing execution");
        auto dir = real_opendir(realpath);

        return dir;
    }

    LOG("Performing consent request to open directory %s", realpath);
    consent_request_cache_fs->consent_request(realpath, gettid(), __FUNCTION__);

    syscall_no_intercept_flag = true;
    auto dir                  = real_opendir(realpath);
    syscall_no_intercept_flag = false;

    LOG("Opened directory with offset %ld", dir);
    opened_directory.insert({reinterpret_cast<unsigned long int>(dir), std::string(realpath)});

    return dir;
}

int closedir(DIR *dirp) {
    START_LOG(capio_syscall(SYS_gettid), "call(dir=%ld)", dirp);

    static int (*real_closedir)(DIR *) = NULL;
    if (!real_closedir) {
        syscall_no_intercept_flag = true;
        real_closedir             = (int (*)(DIR *)) dlsym(RTLD_NEXT, "closedir");
        syscall_no_intercept_flag = false;
        if (!real_closedir) {
            ERR_EXIT("Failed to find original closedir: %s\n", dlerror());
        }
    }

    if (const auto pos = opened_directory.find(reinterpret_cast<unsigned long int>(dirp));
        pos != opened_directory.end()) {
        opened_directory.erase(pos);
        LOG("removed dir from map of opened files");
    }
    syscall_no_intercept_flag = true;
    auto return_code          = real_closedir(dirp);
    syscall_no_intercept_flag = false;
    LOG("Return code of closedir = %d", return_code);

    return return_code;
}

struct dirent *readdir(DIR *dirp) {
    syscall_no_intercept_flag                    = true;
    static struct dirent *(*real_readdir)(DIR *) = NULL;
    if (!real_readdir) {
        real_readdir = (struct dirent * (*) (DIR *) ) dlsym(RTLD_NEXT, "readdir");
    }

    struct dirent *entry;
    while ((entry = real_readdir(dirp)) != NULL) {
        // Example: skip hidden files
        if (entry->d_name[0] == '.') {
            continue;
        }
        printf("[HOOK] readdir: %s\n", entry->d_name);
        return entry;
    }
    syscall_no_intercept_flag = false;

    return NULL; // end of directory
}

struct dirent64 *readdir64(DIR *dirp) {
    syscall_no_intercept_flag                        = true;
    static struct dirent64 *(*real_readdir64)(DIR *) = NULL;
    if (!real_readdir64) {
        real_readdir64 = (struct dirent64 * (*) (DIR *) ) dlsym(RTLD_NEXT, "readdir64");
    }

    struct dirent64 *entry;
    while ((entry = real_readdir64(dirp)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        printf("[HOOK] readdir64: %s\n", entry->d_name);
        return entry;
    }
    syscall_no_intercept_flag = false;
    return NULL;
}

#endif // POSIX_READDIR_HPP
