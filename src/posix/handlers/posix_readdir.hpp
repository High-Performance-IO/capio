#ifndef POSIX_READDIR_HPP
#define POSIX_READDIR_HPP

#include <capio/logger.hpp>
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <utils/requests.hpp>

// Map &DIR -> <dir_path, files already served>
inline std::unordered_map<unsigned long int, std::pair<std::string, int>> opened_directory;

inline std::unordered_map<std::string, std::vector<dirent64 *> *> *directory_items;

inline std::unordered_map<std::string, std::string> directory_commit_token_path;

inline int count_files_in_directory(const char *path) {
    static struct dirent64 *(*real_readdir64)(DIR *) = NULL;
    static DIR *(*real_opendir)(const char *)        = NULL;
    static int (*real_closedir)(DIR *)               = NULL;

    START_LOG(capio_syscall(SYS_gettid), "call(path=%s)", path);
    syscall_no_intercept_flag = true;
    if (!real_readdir64) {
        real_readdir64 = (struct dirent64 * (*) (DIR *) ) dlsym(RTLD_NEXT, "readdir64");
    }

    if (!real_opendir) {
        real_opendir = (DIR * (*) (const char *) ) dlsym(RTLD_NEXT, "opendir");
    }

    if (!real_closedir) {
        real_closedir = (int (*)(DIR *)) dlsym(RTLD_NEXT, "closedir");
    }

    struct dirent64 *entry;
    DIR *dir  = real_opendir(path);
    int count = 0;

    while ((entry = real_readdir64(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            LOG("Entry name is %s... skipping", entry->d_name);
            continue;
        }
        auto dir_abs_path = std::string(path) + "/" + entry->d_name;
        LOG("Directory abs path = %s", dir_abs_path.c_str());

        if (auto directory_object = directory_items->find(dir_abs_path.c_str());
            directory_object == directory_items->end()) {
            LOG("Directory vector not present. Adding it at path %s", path);
            directory_items->emplace(path, new std::vector<dirent64 *>());
        }

        auto directory_object = directory_items->at(path);

        auto itm = std::find_if(directory_object->begin(), directory_object->end(),
                                [&](const dirent64 *_scope_entry) {
                                    return std::string(entry->d_name) == _scope_entry->d_name;
                                });

        if (itm == directory_object->end()) {
            LOG("Item %s is not stored within internal capio data structure. adding it",
                dir_abs_path.c_str());
            auto *new_entry = new dirent64();
            memcpy(new_entry->d_name, entry->d_name, sizeof(entry->d_name));
            new_entry->d_ino    = entry->d_ino;
            new_entry->d_off    = entry->d_off;
            new_entry->d_reclen = entry->d_reclen;
            new_entry->d_type   = entry->d_type;
            directory_object->emplace_back(new_entry);
        }
        count++;
    }

    LOG("Found %ld items.", count);

    real_closedir(dir);
    syscall_no_intercept_flag = false;
    return count;
}

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

    if (directory_items == nullptr) {
        directory_items = new std::unordered_map<std::string, std::vector<dirent64 *> *>();
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
    opened_directory.insert({reinterpret_cast<unsigned long int>(dir), {std::string(realpath), 0}});
    directory_items->emplace(std::string(realpath), new std::vector<dirent64 *>());

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
        if (auto pos1 = directory_items->find(pos->second.first); pos1 != directory_items->end()) {
            directory_items->erase(pos1);
        }
        opened_directory.erase(pos);
        LOG("removed dir from map of opened files");
    }
    syscall_no_intercept_flag = true;
    auto return_code          = real_closedir(dirp);
    syscall_no_intercept_flag = false;
    LOG("Return code of closedir = %d", return_code);

    return return_code;
}

bool capio_internal_Readdir(DIR *dirp, long pid, struct dirent64 *&value1) {
    START_LOG(pid, "call(dirp=%ld)", dirp);

    if (opened_directory.find(reinterpret_cast<unsigned long int>(dirp)) ==
        opened_directory.end()) {
        LOG("Directory is not handled by CAPIO. Returning false");
        return false;
    }

    const auto directory_path =
        std::get<0>(opened_directory.at(reinterpret_cast<unsigned long int>(dirp)));

    if (directory_commit_token_path.find(directory_path) == directory_commit_token_path.end()) {
        char token_path[PATH_MAX];
        posix_directory_committed_request(pid, directory_path.c_str(), token_path);
        directory_commit_token_path.insert({directory_path, token_path});
    }

    const auto token_path = directory_commit_token_path.at(directory_path);

    if (const auto item = opened_directory.find(reinterpret_cast<unsigned long int>(dirp));
        item != opened_directory.end() || std::filesystem::exists(token_path)) {
        LOG("Found dirp.");
        const auto dir_path_name         = std::get<0>(item->second);
        const auto capio_internal_offset = std::get<1>(item->second);

        while (count_files_in_directory(dir_path_name.c_str()) <= capio_internal_offset) {
            LOG("Not enough files... waiting");
            if (std::filesystem::exists(token_path)) {
                value1 = NULL;
                return true;
            }

            struct timespec req;
            req.tv_sec  = 0;
            req.tv_nsec = 100 * 1000000L; // 100 ms
            syscall_no_intercept(SYS_nanosleep, &req, NULL);
        }

        LOG("Returning item %d", std::get<1>(item->second));

        char real_path[PATH_MAX];
        capio_realpath(dir_path_name.c_str(), real_path);

        LOG("Getting files inside directory %s", real_path);

        const auto return_value = directory_items->at(real_path)->at(std::get<1>(item->second));
        std::get<1>(item->second)++;
        value1 = return_value;
        return true;
    }
    return false;
}

struct dirent *readdir(DIR *dirp) {
    long pid = capio_syscall(SYS_gettid);
    START_LOG(pid, "call(dir=%ld)", dirp);

    static struct dirent *(*real_readdir)(DIR *) = NULL;
    if (!real_readdir) {
        LOG("Loading real glibc method");
        syscall_no_intercept_flag = true;
        real_readdir              = (struct dirent * (*) (DIR *) ) dlsym(RTLD_NEXT, "readdir");
        syscall_no_intercept_flag = false;
    }

    struct dirent64 *capio_internal_dirent64;
    if (capio_internal_Readdir(dirp, pid, capio_internal_dirent64)) {
        return reinterpret_cast<dirent *>(capio_internal_dirent64);
    }

    return real_readdir(dirp);
}

struct dirent64 *readdir64(DIR *dirp) {
    long pid = capio_syscall(SYS_gettid);
    START_LOG(pid, "call(dir=%ld)", dirp);

    static struct dirent64 *(*real_readdir64)(DIR *) = NULL;
    if (!real_readdir64) {
        LOG("Loading real glibc method");
        syscall_no_intercept_flag = true;
        real_readdir64            = (struct dirent64 * (*) (DIR *) ) dlsym(RTLD_NEXT, "readdir64");
        syscall_no_intercept_flag = false;
    }

    struct dirent64 *capio_internal_dirent64;
    if (capio_internal_Readdir(dirp, pid, capio_internal_dirent64)) {
        return capio_internal_dirent64;
    }

    return real_readdir64(dirp);
}

#endif // POSIX_READDIR_HPP
