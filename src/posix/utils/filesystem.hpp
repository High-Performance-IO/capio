#ifndef CAPIO_POSIX_UTILS_FILESYSTEM_HPP
#define CAPIO_POSIX_UTILS_FILESYSTEM_HPP

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

#include "capio/env.hpp"
#include "capio/filesystem.hpp"
#include "capio/logger.hpp"
#include "capio/syscall.hpp"

#include "types.hpp"

CPFileDescriptors_t *capio_files_descriptors;
CPFilesPaths_t *capio_files_paths;
const std::string *current_dir;
CPFiles_t *files;

/**
 * Set the CLOEXEC property of a file descriptor in metadata structures
 * @param fd
 * @param is_cloexec
 * @return
 */
inline void set_capio_fd_cloexec(int fd, bool is_cloexec) {
    std::get<3>(files->at(fd)) = is_cloexec;
}

/**
 * Get the current directory
 * @return the current directory
 */
inline const std::string *get_current_dir() { return current_dir; }

auto create_capio_posix_shm(long tid, int fd) {
    std::string shm_name("offset_" + std::to_string(tid) + "_" + std::to_string(fd));
    START_LOG(tid, "call(shm_name=%s)", shm_name.c_str());
    syscall_no_intercept_flag = true;
    auto *p_offset            = static_cast<off64_t *>(create_shm(shm_name, sizeof(off64_t)));
    syscall_no_intercept_flag = false;
    return p_offset;
}

/**
 * Add a path to metadata structures
 * @param path
 * @return
 */
inline void add_capio_path(const std::string &path) {
    capio_files_paths->emplace(path, std::unordered_set<int>{});
}

/**
 * Add a file descriptor to metadata structures
 * @param path
 * @param fd
 * @return
 */
inline void add_capio_fd(long tid, const std::string &path, int fd, off64_t offset,
                         off64_t init_size, int flags, bool is_cloexec) {
    add_capio_path(path);
    capio_files_paths->at(path).insert(fd);
    capio_files_descriptors->insert({fd, path});

    auto p_offset = create_capio_posix_shm(tid, fd);
    *p_offset     = offset;
    files->insert({fd, {p_offset, init_size, flags, is_cloexec}});
}

/**
 * Compute the absolute path for @pathname
 * @param pathname
 * @return
 */
const std::string *capio_posix_realpath(const std::string *pathname) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(path=%s)", pathname->c_str());
    char *posix_real_path = capio_realpath((char *) pathname->c_str(), nullptr);

    // if capio_realpath fails, then it should be a capio_file
    if (posix_real_path == nullptr) {
        LOG("path is null due to errno='%s'", strerror(errno));

        const std::string *capio_dir = get_capio_dir();
        /*
         * The other condition in the if is given to check for a limit case in which the
         * current_dir is not the capio_dir, but the pathname is an absolute path to capio_dir
         */
        if (current_dir->find(*capio_dir) != std::string::npos ||
            pathname->find(*capio_dir) != std::string::npos) {
            if (!is_absolute(pathname)) {
                auto new_path = new std::string(*current_dir + "/" + *pathname);

                // remove /./ from path
                std::size_t pos = 0;
                while ((pos = new_path->find("/./", pos)) != std::string::npos) {
                    new_path->replace(new_path->find("/./"), 3, "/");
                    pos += 1;
                }

                LOG("Computed absolute path = %s", new_path->c_str());
                return new_path;
            } else {
                LOG("Path=%s is already absolute", pathname->c_str());
            }
            return pathname;
        } else {
            // if file not found, then nullptr is returned and errno can be read
            LOG("file %s is not a posix file, nor a capio file!", pathname->c_str());
            return new std::string("");
        }
    }

    // if not, then check for realpath through libc implementation
    LOG("Computed realpath = %s", posix_real_path);
    return new std::string(posix_real_path);
}

/**
 * Delete a file descriptor from metadata structures
 * @param fd
 * @return
 */
inline void delete_capio_fd(int fd) {
    const std::string &path = capio_files_descriptors->at(fd);
    capio_files_paths->at(path).erase(fd);
    capio_files_descriptors->erase(fd);
    files->erase(fd);
}

/**
 * Delete a file or folder from metadata structures
 * @param path
 * @return
 */
inline void delete_capio_path(const std::string &path) {
    for (auto fd : capio_files_paths->at(path)) {
        delete_capio_fd(fd);
    }
    capio_files_paths->erase(path);
}

/**
 * Destroy metadata structures
 * @return
 */
inline void destroy_filesystem() {
    delete current_dir;
    delete capio_files_descriptors;
    delete capio_files_paths;
    delete files;
}

/**
 * Clone a file descriptor in metadata structures
 * @param oldfd
 * @param newfd
 * @return
 */
inline void dup_capio_fd(long tid, int oldfd, int newfd, bool is_cloexec) {
    const std::string &path = capio_files_descriptors->at(oldfd);
    capio_files_paths->at(path).insert(newfd);
    files->insert({newfd, files->at(oldfd)});
    set_capio_fd_cloexec(newfd, is_cloexec);
    capio_files_descriptors->insert({newfd, capio_files_descriptors->at(oldfd)});

    create_capio_posix_shm(tid, newfd);
}

/**
 * Check if a file descriptor exists in metadata structures
 * @param fd
 * @return if the file descriptor exists
 */
inline bool exists_capio_fd(int fd) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%d)", fd);
    return files->find(fd) != files->end();
}

/**
 * Check if a path exists in metadata structures
 * @param path
 * @return if the path exists
 */
inline bool exists_capio_path(const std::string &path) {
    return capio_files_paths->find(path) != capio_files_paths->end();
}

/**
 * Get the CLOEXEC property of a file descriptor in metadata structures
 * @param fd
 * @return the CLOEXEC property
 */
inline bool get_capio_fd_cloexec(int fd) { return std::get<3>(files->at(fd)); }

/**
 * Get the active flags of a file descriptor in metadata structures
 * @param fd
 * @return the active flags
 */
inline bool get_capio_fd_flags(int fd) { return std::get<2>(files->at(fd)); }

/**
 * Get the path of a file descriptor
 * @param fd
 * @return the file descriptor path
 */
inline const std::string &get_capio_fd_path(int fd) { return capio_files_descriptors->at(fd); }

/**
 * Get the current offset of a file descriptor
 * @param fd
 * @return the current offset
 */
inline off64_t get_capio_fd_offset(int fd) { return *std::get<0>(files->at(fd)); }

/**
 * Get the actual size of a file descriptor
 * @param fd
 * @return the actual size of the file
 */
inline off64_t get_capio_fd_size(int fd) { return std::get<1>(files->at(fd)); }

/**
 * Get all the file descriptors stored in metadata structures
 * @return a vector of file descriptors
 */
inline std::vector<int> get_capio_fds() {
    std::vector<int> fds;
    fds.reserve(files->size());
    for (auto &file : *files) {
        fds.push_back(file.first);
    }
    return fds;
}

/**
 * Get the corresponding path from a dirfd file descriptor
 * @param dirfd
 * @return the corresponding path
 */
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

/**
 * Initialize metadata structures
 * @return
 */
inline void init_filesystem() {
    char *buf = static_cast<char *>(malloc(PATH_MAX * sizeof(char)));
    syscall_no_intercept(SYS_getcwd, buf, PATH_MAX);
    current_dir             = new std::string(buf);
    capio_files_descriptors = new CPFileDescriptors_t();
    capio_files_paths       = new CPFilesPaths_t();
    files                   = new CPFiles_t();
}

/**
 * Rename a file or folder in metadata structures
 * @param path
 * @return
 */
inline void rename_capio_path(const std::string &oldpath, const std::string &newpath) {
    auto entry  = capio_files_paths->extract(oldpath);
    entry.key() = newpath;
    capio_files_paths->insert(std::move(entry));
    for (auto fd : capio_files_paths->at(newpath)) {
        capio_files_descriptors->at(fd).assign(newpath);
    }
}

/**
 * Modify the active flags of a file descriptor in metadata structures
 * @param fd
 * @param flags
 * @return
 */
inline void set_capio_fd_flags(int fd, int flags) { std::get<2>(files->at(fd)) = flags; }

/**
 * Set the offset of a file descriptor in metadata structures
 * @param fd
 * @param offset
 * @return
 */
inline void set_capio_fd_offset(int fd, off64_t offset) { *std::get<0>(files->at(fd)) = offset; }

/**
 * Change the current directory
 * @return the current directory
 */
inline void set_current_dir(const std::string *cwd) {
    delete current_dir;
    current_dir = cwd;
}

#endif // CAPIO_POSIX_UTILS_FILESYSTEM_HPP
