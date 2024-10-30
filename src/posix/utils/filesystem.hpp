#ifndef CAPIO_POSIX_UTILS_FILESYSTEM_HPP
#define CAPIO_POSIX_UTILS_FILESYSTEM_HPP

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "capio/env.hpp"
#include "capio/filesystem.hpp"
#include "capio/logger.hpp"
#include "capio/syscall.hpp"

#include "types.hpp"

inline CPFileDescriptors_t *capio_files_descriptors;
inline CPFilesPaths_t *capio_files_paths;
inline std::unique_ptr<std::filesystem::path> current_dir;
inline CPFiles_t *files;

inline thread_local std::vector<std::string> *paths_to_store_in_memory;

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
inline const std::filesystem::path &get_current_dir() { return *current_dir; }

/**
 * Add a path to metadata structures
 * @param path
 * @return
 */
inline void add_capio_path(const std::string &path) {
    capio_files_paths->emplace(path, std::unordered_set<int>{});
}

/**
 * Add a file descriptor to metadata structures. is_cloexec is used to handle whether the fd needs
 * to be closed before executing an execve or not (fd persists if is_cloexec == true)
 * @param path
 * @param fd
 * @return
 */
inline void add_capio_fd(pid_t tid, const std::string &path, int fd, capio_off64_t offset,
                         bool is_cloexec) {
    START_LOG(tid, "call(path=%s, fd=%d)", path.c_str(), fd);
    add_capio_path(path);
    LOG("Added capio path %s", path.c_str());
    capio_files_paths->at(path).insert(fd);
    LOG("Inserted tid %d for path %s", tid, path.c_str());
    capio_files_descriptors->insert({fd, path});
    LOG("Inserted file descriptor tuple");
    files->insert({fd, {std::make_shared<capio_off64_t>(offset), 0, 0, is_cloexec}});
    LOG("Registered file");
}

/**
 * Compute the absolute path for @pathname
 * @param pathname
 * @return
 */
inline std::filesystem::path capio_posix_realpath(const std::filesystem::path &pathname) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(path=%s)", pathname.c_str());
    char *posix_real_path = capio_realpath((char *) pathname.c_str(), nullptr);

    // if capio_realpath fails, then it should be a capio_file
    if (posix_real_path == nullptr) {
        LOG("path is null due to errno='%s'", strerror(errno));

        if (pathname.is_absolute()) {
            LOG("Path=%s is already absolute", pathname.c_str());
            return {pathname};
        } else {
            std::filesystem::path new_path = (*current_dir / pathname).lexically_normal();
            if (is_capio_path(new_path)) {
                LOG("Computed absolute path = %s", new_path.c_str());
                return new_path;
            } else {
                LOG("file %s is not a posix file, nor a capio file!", pathname.c_str());
                return {};
            }
        }
    }

    // if not, then check for realpath through libc implementation
    LOG("Computed realpath = %s", posix_real_path);
    return {posix_real_path};
}

/**
 * Return the absolute path for the @path argument
 *
 * @param path
 * @return
 */
inline std::filesystem::path capio_absolute(const std::filesystem::path &path) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s)", path.c_str());
    if (!path.is_absolute()) {
        LOG("PATH is not absolute");
        auto absolute = capio_posix_realpath(path);
        LOG("Computed absolute path is %s", absolute.c_str());
        return absolute;
    }
    LOG("Path is absolute");
    return path;
}

/**
 * Delete a file descriptor from metadata structures
 * @param fd
 * @return
 */
inline void delete_capio_fd(pid_t fd) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d)", fd);
    auto &path = capio_files_descriptors->at(fd);
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
    START_LOG(syscall_no_intercept(SYS_gettid), "call(path=%s)", path.c_str());
    if (capio_files_paths->find(path) != capio_files_paths->end()) {
        auto it = capio_files_paths->at(path).begin();
        LOG("Proceeding to remove fds");
        while (it != capio_files_paths->at(path).end()) {
            delete_capio_fd(*it++);
        }
        LOG("Proceeding to remove path from capio_files_paths");
        capio_files_paths->erase(path);
        LOG("Cleanup completed");
    }
}

/**
 * Destroy metadata structures
 * @return
 */
inline void destroy_filesystem() {
    current_dir.reset();
    delete capio_files_descriptors;
    delete capio_files_paths;
    delete files;
}

/**
 * Clone a file descriptor in metadata structures
 * @param tid
 * @param oldfd
 * @param newfd
 * @return
 */
inline void dup_capio_fd(pid_t tid, int oldfd, int newfd, bool is_cloexec) {
    const std::string &path = capio_files_descriptors->at(oldfd);
    capio_files_paths->at(path).insert(newfd);
    files->insert({newfd, files->at(oldfd)});
    set_capio_fd_cloexec(newfd, is_cloexec);
    capio_files_descriptors->insert({newfd, capio_files_descriptors->at(oldfd)});
}

/**
 * Check if a file descriptor exists in metadata structures
 * @param fd
 * @return if the file descriptor exists
 */
inline bool exists_capio_fd(pid_t fd) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%d)", fd);
    return files->find(fd) != files->end();
}

/**
 * Get the CLOEXEC property of a file descriptor in metadata structures
 * @param fd
 * @return the CLOEXEC property
 */
inline bool get_capio_fd_cloexec(int fd) { return std::get<3>(files->at(fd)); }

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
inline capio_off64_t get_capio_fd_offset(int fd) { return *std::get<0>(files->at(fd)); }

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
inline std::filesystem::path get_dir_path(int dirfd) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(dirfd=%d)", dirfd);

    auto it = capio_files_descriptors->find(dirfd);
    if (it != capio_files_descriptors->end()) {
        LOG("dirfd %d points to path %s", dirfd, it->second.c_str());
        return it->second;
    }
    LOG("dirfd %d not found. Computing it through proclnk", dirfd);
    char proclnk[128]           = {};
    char dir_pathname[PATH_MAX] = {};
    sprintf(proclnk, "/proc/self/fd/%d", dirfd);
    if (syscall_no_intercept(SYS_readlink, proclnk, dir_pathname, PATH_MAX) < 0) {
        LOG("failed to readlink\n");
        return {};
    }
    LOG("dirfd %d points to path %s", dirfd, dir_pathname);
    return {dir_pathname};
}

/**
 * Initialize metadata structures
 * @return
 */
inline void init_filesystem() {
    std::unique_ptr<char[]> buf(new char[PATH_MAX]);
    syscall_no_intercept(SYS_getcwd, buf.get(), PATH_MAX);
    current_dir             = std::make_unique<std::filesystem::path>(buf.get());
    capio_files_descriptors = new CPFileDescriptors_t();
    capio_files_paths       = new CPFilesPaths_t();
    files                   = new CPFiles_t();
}

/**
 * Rename a file or folder in metadata structures
 * @param oldpath
 * @param newpath
 * @return
 */
inline void rename_capio_path(const std::string &oldpath, const std::string &newpath) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(oldpath=%s, newpath=%s)", oldpath.c_str(),
              newpath.c_str());
    if (capio_files_paths->find(oldpath) != capio_files_paths->end()) {
        auto entry  = capio_files_paths->extract(oldpath);
        entry.key() = newpath;
        capio_files_paths->insert(std::move(entry));
        for (auto fd : capio_files_paths->at(newpath)) {
            capio_files_descriptors->at(fd).assign(newpath);
        }
    } else {
        LOG("Warning: olpath not found in capio_files_paths");
    }
}

/**
 * Set the offset of a file descriptor in metadata structures
 * @param fd
 * @param offset
 * @return
 */
inline void set_capio_fd_offset(int fd, capio_off64_t offset) {
    *std::get<0>(files->at(fd)) = offset;
}

/**
 * Change the current directory
 * @return the current directory
 */
inline void set_current_dir(const std::filesystem::path &cwd) {
    current_dir = std::make_unique<std::filesystem::path>(cwd);
}

#endif // CAPIO_POSIX_UTILS_FILESYSTEM_HPP
