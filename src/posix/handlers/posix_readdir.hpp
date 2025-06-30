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
inline std::unordered_map<unsigned long int, std::pair<std::string, int>> *opened_directory =
    nullptr;

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

        std::filesystem::path dir_abs_path(entry->d_name);

        if (!(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)) {
            LOG("Entry name is %s. computing absolute path", entry->d_name);
            dir_abs_path = std::filesystem::path(path) / entry->d_name;
            LOG("Directory abs path = %s", dir_abs_path.c_str());
        }

        if (directory_items->find(path) == directory_items->end()) {
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

    auto tmp = std::string(name);

    START_LOG(capio_syscall(SYS_gettid), "call(path=%s)", tmp.c_str());

    auto absolute_path = capio_absolute(name);

    LOG("Resolved absolute path = %s", absolute_path.c_str());

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

    if (opened_directory == nullptr) {
        opened_directory = new std::unordered_map<unsigned long, std::pair<std::string, int>>();
    }

    if (!is_capio_path(absolute_path)) {
        LOG("Not a CAPIO path. continuing execution");
        syscall_no_intercept_flag = true;
        auto dir                  = real_opendir(absolute_path.c_str());
        syscall_no_intercept_flag = false;

        return dir;
    }

    LOG("Performing consent request to open directory %s", absolute_path.c_str());
    consent_request_cache_fs->consent_request(absolute_path.c_str(), gettid(), __FUNCTION__);

    syscall_no_intercept_flag = true;
    auto dir                  = real_opendir(absolute_path.c_str());
    syscall_no_intercept_flag = false;

    LOG("Opened directory with offset %ld", dir);
    opened_directory->insert(
        {reinterpret_cast<unsigned long int>(dir), {std::string(absolute_path), 0}});
    directory_items->emplace(std::string(absolute_path), new std::vector<dirent64 *>());

    auto fd = dirfd(dir);
    LOG("File descriptor for directory %s is %d", absolute_path.c_str(), fd);

    add_capio_fd(capio_syscall(SYS_gettid), absolute_path.c_str(), fd, 0, 0);

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

    if (const auto pos = opened_directory->find(reinterpret_cast<unsigned long int>(dirp));
        pos != opened_directory->end()) {
        LOG("Closing directory with path %s", pos->second.first.c_str());
        close_request(pos->second.first.c_str(), capio_syscall(SYS_gettid));
        syscall_no_intercept_flag = true;
        delete_capio_fd(dirfd(dirp));
        syscall_no_intercept_flag = false;

        if (auto pos1 = directory_items->find(pos->second.first); pos1 != directory_items->end()) {
            directory_items->erase(pos1);
        }
        opened_directory->erase(pos);
        LOG("removed dir from map of opened files");
    }

    syscall_no_intercept_flag = true;
    auto return_code          = real_closedir(dirp);
    syscall_no_intercept_flag = false;
    LOG("Return code of closedir = %d", return_code);

    return return_code;
}

inline struct dirent64 *capio_internal_readdir(DIR *dirp, long pid) {
    START_LOG(pid, "call(dirp=%ld)", dirp);

    auto directory_path =
        std::get<0>(opened_directory->at(reinterpret_cast<unsigned long int>(dirp)));

    if (directory_commit_token_path.find(directory_path) == directory_commit_token_path.end()) {
        LOG("Commit token path was not found for path %s", directory_path.c_str());
        auto token_path = new char[PATH_MAX]{0};
        posix_directory_committed_request(pid, directory_path, token_path);
        LOG("Inserting token path %s", token_path);
        directory_commit_token_path.insert({directory_path, token_path});
    }

    const auto token_path = directory_commit_token_path.at(directory_path);

    if (const auto item = opened_directory->find(reinterpret_cast<unsigned long int>(dirp));
        item != opened_directory->end() || std::filesystem::exists(token_path)) {
        LOG("Found dirp.");
        const auto dir_path_name         = std::get<0>(item->second);
        const auto capio_internal_offset = std::get<1>(item->second);

        auto files_in_directory = count_files_in_directory(dir_path_name.c_str());
        LOG("There are %ld files inside %s", files_in_directory, dir_path_name.c_str());
        while (files_in_directory <= capio_internal_offset) {
            LOG("Not enough files: expected %ld, got %ld... waiting", files_in_directory,
                capio_internal_offset);
            LOG("Checking for commit token existence (%s)", token_path.c_str());
            syscall_no_intercept_flag = true;
            bool is_committed         = std::filesystem::exists(token_path);
            syscall_no_intercept_flag = false;
            LOG("File %s committed", is_committed ? "is" : "is not");
            if (is_committed) {
                LOG("Returning NULL as result");
                errno = 0;
                return NULL;
            }

            struct timespec req{0};
            req.tv_sec  = 0;
            req.tv_nsec = 100 * 1000000L; // 100 ms
            syscall_no_intercept(SYS_nanosleep, &req, NULL);
            files_in_directory = count_files_in_directory(dir_path_name.c_str());
            LOG("There are %ld files inside %s", files_in_directory, dir_path_name.c_str());
        }

        LOG("Returning item %d", std::get<1>(item->second));

        char real_path[PATH_MAX];
        capio_realpath(dir_path_name.c_str(), real_path);

        LOG("Getting files inside directory %s", real_path);

        const auto return_value = directory_items->at(real_path)->at(std::get<1>(item->second));
        std::get<1>(item->second)++;

        LOG("Returned dirent structure:");
        LOG("dirent.d_name   = %s", return_value->d_name);
        LOG("dirent.d_type   = %d", return_value->d_type);
        LOG("dirent.d_ino    = %d", return_value->d_ino);
        LOG("dirent.d_off    = %d", return_value->d_off);
        LOG("dirent.d_reclen = %d", return_value->d_reclen);
        return return_value;
    }
    LOG("Reached end of branch... something might be amiss.. returning EOS");
    errno = 0;
    return NULL;
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

    if (opened_directory->find(reinterpret_cast<unsigned long int>(dirp)) ==
        opened_directory->end()) {
        LOG("Directory is not handled by CAPIO. Returning false");
        syscall_no_intercept_flag = true;
        auto result               = real_readdir(dirp);
        syscall_no_intercept_flag = false;

        return result;
    }

    struct dirent64 *capio_internal_dirent64 = capio_internal_readdir(dirp, pid);
    LOG("return value == NULL ? %s", capio_internal_dirent64 == NULL ? "TRUE" : "FALSE");
    return reinterpret_cast<dirent *>(capio_internal_dirent64);
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

    if (opened_directory->find(reinterpret_cast<unsigned long int>(dirp)) ==
        opened_directory->end()) {
        LOG("Directory is not handled by CAPIO. Returning false");
        syscall_no_intercept_flag = true;
        auto result               = real_readdir64(dirp);
        syscall_no_intercept_flag = false;

        return result;
    }

    auto capio_internal_dirent64 = capio_internal_readdir(dirp, pid);
    LOG("return value == NULL ? %s", capio_internal_dirent64 == NULL ? "TRUE" : "FALSE");
    return capio_internal_dirent64;
}

#endif // POSIX_READDIR_HPP
