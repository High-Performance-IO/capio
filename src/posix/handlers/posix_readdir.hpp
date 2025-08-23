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
inline std::unordered_map<unsigned long int, std::pair<std::string, long unsigned int>>
    *opened_directory = nullptr;

inline std::unordered_map<std::string, std::vector<dirent64 *> *> *directory_items;

inline std::unordered_map<std::string, std::string> directory_commit_token_path;

inline timespec dirent_await_sleep_time{0, 100 * 1000000L}; // 100ms

inline dirent64 *(*real_readdir64)(DIR *) = nullptr;
inline dirent *(*real_readdir)(DIR *)     = nullptr;
inline DIR *(*real_opendir)(const char *) = nullptr;
inline int (*real_closedir)(DIR *)        = nullptr;

inline dirent64 *dirent_curr_dir;
inline dirent64 *dirent_parent_dir;

inline void init_posix_dirent() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    syscall_no_intercept_flag = true;
    if (!real_readdir64) {
        LOG("Loading real readdir64 method");
        real_readdir64 = (dirent64 * (*) (DIR *) ) dlsym(RTLD_NEXT, "readdir64");
    }

    if (!real_opendir) {
        LOG("Loading real opendir method");
        real_opendir = (DIR * (*) (const char *) ) dlsym(RTLD_NEXT, "opendir");
    }

    if (!real_closedir) {
        LOG("Loading real closedir method");
        real_closedir = (int (*)(DIR *)) dlsym(RTLD_NEXT, "closedir");
    }

    if (!real_readdir) {
        LOG("Loading real readdir method");
        real_readdir = (dirent * (*) (DIR *) ) dlsym(RTLD_NEXT, "readdir");
    }

    directory_items = new std::unordered_map<std::string, std::vector<dirent64 *> *>();
    opened_directory =
        new std::unordered_map<unsigned long, std::pair<std::string, unsigned long int>>();

    dirent_curr_dir   = new dirent64();
    dirent_parent_dir = new dirent64();

    dirent_curr_dir->d_type   = DT_DIR;
    dirent_parent_dir->d_type = DT_DIR;

    memcpy(dirent_curr_dir->d_name, ".\0", 2);
    memcpy(dirent_parent_dir->d_name, "..\0", 3);

    syscall_no_intercept_flag = false;
}

inline unsigned long int load_files_from_directory(const char *path) {

    START_LOG(capio_syscall(SYS_gettid), "call(path=%s)", path);

    syscall_no_intercept_flag = true;
    dirent64 *entry;
    DIR *dir                = real_opendir(path);
    unsigned long int count = 0;

    if (directory_items->find(path) == directory_items->end()) {
        LOG("Directory vector not present. Adding it at path %s", path);
        directory_items->emplace(path, new std::vector<dirent64 *>());
        directory_items->at(path)->emplace_back(dirent_curr_dir);
        directory_items->at(path)->emplace_back(dirent_parent_dir);
    }

    while ((entry = real_readdir64(dir)) != NULL) {
        std::filesystem::path dir_abs_path(entry->d_name);

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            LOG("Skipping entry %s", entry->d_name);
            continue;
        }

        LOG("Entry name is %s. computing absolute path", entry->d_name);
        dir_abs_path = std::filesystem::path(path) / entry->d_name;
        LOG("Directory abs path = %s", dir_abs_path.c_str());

        auto directory_object = directory_items->at(path);

        auto itm = std::find_if(directory_object->begin(), directory_object->end(),
                                [&](const dirent64 *_scope_entry) {
                                    return std::string(entry->d_name) == _scope_entry->d_name;
                                });

        if (itm == directory_object->end()) {
            LOG("Item %s is not stored within internal capio data structure. adding it",
                dir_abs_path.c_str());
            auto new_entry = new dirent64();
            memcpy(new_entry, entry, sizeof(dirent64));
            directory_object->emplace_back(new_entry);
        }
        count++;
    }

    LOG("Found %ld items.", count);

    real_closedir(dir);
    syscall_no_intercept_flag = false;
    return count;
}

inline struct dirent64 *capio_internal_readdir(DIR *dirp, long pid) {
    START_LOG(pid, "call(dirp=%ld)", dirp);

    auto directory_path =
        std::get<0>(opened_directory->at(reinterpret_cast<unsigned long int>(dirp)));

    const auto &committed_directory_toke_path = directory_commit_token_path.at(directory_path);

    if (const auto item = opened_directory->find(reinterpret_cast<unsigned long int>(dirp));
        item != opened_directory->end() || std::filesystem::exists(committed_directory_toke_path)) {
        LOG("Found dirp.");
        const auto dir_path_name         = std::get<0>(item->second);
        const auto capio_internal_offset = std::get<1>(item->second);

        LOG("Getting files inside directory %s", dir_path_name.c_str());

        if (capio_internal_offset >= directory_items->at(dir_path_name)->size()) {
            LOG("Internal offset for dir reached end of vector. Loading files from FS.");
            auto loaded_files = load_files_from_directory(dir_path_name.c_str());
            LOG("There are %ld files inside %s", loaded_files, dir_path_name.c_str());
            while (loaded_files <= capio_internal_offset) {
                LOG("Not enough files: expected %ld, got %ld... waiting", loaded_files,
                    capio_internal_offset);
                LOG("Checking for commit token existence (%s)",
                    committed_directory_toke_path.c_str());
                syscall_no_intercept_flag = true;
                bool is_committed         = std::filesystem::exists(committed_directory_toke_path);
                syscall_no_intercept_flag = false;
                LOG("File %s committed", is_committed ? "is" : "is not");
                if (is_committed) {
                    LOG("Returning NULL as result");
                    errno = 0;
                    return NULL;
                }

                syscall_no_intercept(SYS_nanosleep, &dirent_await_sleep_time, NULL);
                loaded_files = load_files_from_directory(dir_path_name.c_str());
                LOG("There are %ld files inside %s", loaded_files, dir_path_name.c_str());
            }
        }

        LOG("Returning item %d", std::get<1>(item->second));

        const auto return_value = directory_items->at(dir_path_name)->at(std::get<1>(item->second));
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

DIR *opendir(const char *name) {

    START_LOG(capio_syscall(SYS_gettid), "call(path=%s)", name);

    if (is_forbidden_path(name)) {
        LOG("Path %s is forbidden: skip", name);
        syscall_no_intercept_flag = true;
        auto res                  = real_opendir(name);
        syscall_no_intercept_flag = false;
        return res;
    }

    auto absolute_path = capio_absolute(name);

    LOG("Resolved absolute path = %s", absolute_path.c_str());

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

    if (directory_commit_token_path.find(absolute_path) == directory_commit_token_path.end()) {
        LOG("Commit token path was not found for path %s", absolute_path.c_str());
        auto token_path = new char[PATH_MAX]{0};
        posix_directory_committed_request(capio_syscall(SYS_gettid), absolute_path, token_path);
        LOG("Inserting token path %s", token_path);
        directory_commit_token_path.insert({absolute_path, token_path});
    }

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

struct dirent *readdir(DIR *dirp) {
    long pid = capio_syscall(SYS_gettid);
    START_LOG(pid, "call(dir=%ld)", dirp);

    if (opened_directory->find(reinterpret_cast<unsigned long int>(dirp)) ==
        opened_directory->end()) {
        LOG("Directory is not handled by CAPIO. Returning read readdir");
        syscall_no_intercept_flag = true;
        const auto result         = real_readdir(dirp);
        syscall_no_intercept_flag = false;

        return result;
    }

    dirent64 *capio_internal_dirent64 = capio_internal_readdir(dirp, pid);
    LOG("return value == NULL ? %s", capio_internal_dirent64 == NULL ? "TRUE" : "FALSE");
    return reinterpret_cast<dirent *>(capio_internal_dirent64);
}

struct dirent64 *readdir64(DIR *dirp) {
    long pid = capio_syscall(SYS_gettid);
    START_LOG(pid, "call(dir=%ld)", dirp);

    if (opened_directory->find(reinterpret_cast<unsigned long int>(dirp)) ==
        opened_directory->end()) {
        LOG("Directory is not handled by CAPIO. Returning read readdir");
        syscall_no_intercept_flag = true;
        auto result               = real_readdir64(dirp);
        syscall_no_intercept_flag = false;

        return result;
    }

    auto capio_internal_dirent64 = capio_internal_readdir(dirp, pid);
    LOG("return value == NULL ? %s", capio_internal_dirent64 == NULL ? "TRUE" : "FALSE");
    return capio_internal_dirent64;
}

void rewinddir(DIR *dirp) {
    long pid = capio_syscall(SYS_gettid);
    START_LOG(pid, "call(dir=%ld)", dirp);

    static void (*real_rewinddir)(DIR *) = NULL;
    if (!real_rewinddir) {
        LOG("Loading real glibc method");
        syscall_no_intercept_flag = true;
        real_rewinddir            = (void (*)(DIR *)) dlsym(RTLD_NEXT, "rewinddir");
        syscall_no_intercept_flag = false;
    }

    if (opened_directory->find(reinterpret_cast<unsigned long int>(dirp)) ==
        opened_directory->end()) {
        LOG("Directory is not handled by CAPIO. Returning false");
        syscall_no_intercept_flag = true;
        real_rewinddir(dirp);
        syscall_no_intercept_flag = false;
    } else {
        LOG("File handled by CAPIO. Resetting internal CAPIO offset");
        opened_directory->at(reinterpret_cast<unsigned long int>(dirp)).second = 0;
    }
}

long int telldir(DIR *dirp) {
    long pid = capio_syscall(SYS_gettid);
    START_LOG(pid, "call(dir=%ld)", dirp);

    static long int (*real_telldir)(DIR *) = NULL;
    if (!real_telldir) {
        LOG("Loading real glibc method");
        syscall_no_intercept_flag = true;
        real_telldir              = (long int (*)(DIR *)) dlsym(RTLD_NEXT, "telldir");
        syscall_no_intercept_flag = false;
    }

    if (opened_directory->find(reinterpret_cast<unsigned long int>(dirp)) ==
        opened_directory->end()) {
        LOG("Directory is not handled by CAPIO. Returning false");
        syscall_no_intercept_flag = true;
        auto result               = real_telldir(dirp);
        syscall_no_intercept_flag = false;
        LOG("Telldir returned %ld", result);
        return result;
    }

    LOG("File handled by CAPIO. Returning internal CAPIO offset (which is %ld)",
        opened_directory->at(reinterpret_cast<unsigned long int>(dirp)).second);
    return opened_directory->at(reinterpret_cast<unsigned long int>(dirp)).second;
}

void seekdir(DIR *dirp, long int loc) {
    long pid = capio_syscall(SYS_gettid);
    START_LOG(pid, "call(dir=%ld, loc=%ld)", dirp, loc);

    static void (*real_seekdir)(DIR *, long int) = NULL;
    if (!real_seekdir) {
        LOG("Loading real glibc method");
        syscall_no_intercept_flag = true;
        real_seekdir              = (void (*)(DIR *, long int)) dlsym(RTLD_NEXT, "seekdir");
        syscall_no_intercept_flag = false;
    }

    if (opened_directory->find(reinterpret_cast<unsigned long int>(dirp)) ==
        opened_directory->end()) {
        LOG("Directory is not handled by CAPIO. Using real seekdir");
        syscall_no_intercept_flag = true;
        real_seekdir(dirp, loc);
        syscall_no_intercept_flag = false;
    } else {
        LOG("Directory is handled by CAPIO. Setting internal offset to %ld", loc);
        opened_directory->at(reinterpret_cast<unsigned long int>(dirp)).second = loc;
    }
}

int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result) {

    /*
     * WARN: I have not yet clear the usage of this function, as such bugs are surely presents
     * TODO: implement the correct handling logic for this method
     */
    long pid = capio_syscall(SYS_gettid);
    START_LOG(pid, "call(dir=%ld)", dirp);

    static int (*real_readdir_r)(DIR *, struct dirent *, struct dirent **) = NULL;
    if (!real_readdir_r) {
        LOG("Loading real glibc method");
        syscall_no_intercept_flag = true;
        real_readdir_r =
            (int (*)(DIR *, struct dirent *, struct dirent **)) dlsym(RTLD_NEXT, "readdir_r");
        syscall_no_intercept_flag = false;
    }

    if (opened_directory->find(reinterpret_cast<unsigned long int>(dirp)) ==
        opened_directory->end()) {
        LOG("Directory is not handled by CAPIO. Using real readdir_r");
        syscall_no_intercept_flag = true;
        int res                   = real_readdir_r(dirp, entry, result);
        syscall_no_intercept_flag = false;
        return res;
    }

    *result = reinterpret_cast<dirent *>(capio_internal_readdir(dirp, pid));

    LOG("CAPIO returned a directory entry: %s", entry->d_name);
    return 0;
}

#endif // POSIX_READDIR_HPP
