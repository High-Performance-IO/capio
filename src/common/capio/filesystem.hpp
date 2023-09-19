#ifndef CAPIO_COMMON_FILESYSTEM_HPP
#define CAPIO_COMMON_FILESYSTEM_HPP

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include "syscall.hpp"
#include <sys/stat.h>
#include "logger.hpp"

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

    struct stat path_stat{};
    int tmp = fstat(dirfd, &path_stat);
    if (tmp != 0) {
        LOG("Error at is_directory(dirfd=%d) -> %d: %d (%s)", dirfd, tmp, errno, std::strerror(errno));
        return -1;
    }
    return S_ISDIR(path_stat.st_mode) == 1;
}

inline bool is_directory(const std::string &path) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s)", path.c_str());

    struct stat statbuf{};
    if (stat(path.c_str(), &statbuf) != 0) {
        LOG("Error at is_directory(path=%d) -> %d: %d (%s)", path.c_str(), errno, std::strerror(errno));
        return -1;
    }
    return S_ISDIR(statbuf.st_mode) == 1;
}

bool is_prefix(std::string path_1, std::string path_2) {
    auto res = std::mismatch(path_1.begin(), path_1.end(), path_2.begin());
    return res.first == path_2.end();
}


#ifndef PATH_MAX
#ifdef _POSIX_VERSION
#define PATH_MAX 1024
#else
#ifdef MAXPATHLEN
#define PATH_MAX MAXPATHLEN
#else
#define PATH_MAX 1024
#endif
#endif
#endif

#define MAX_READLINKS 32

char *capio_realpath(const char *path, char *resolved) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, resolved=%s)", path, resolved);
    char copy_path[PATH_MAX];
    char *max_path, *new_path, *allocated_path;
    size_t path_len;
    int readlinks = 0;
#ifdef S_IFLNK
    int link_len;
#endif

    if (path == nullptr) {
        errno = EINVAL;
        LOG("path==nullptr");
        return nullptr;
    }
    if (*path == '\0') {
        errno = ENOENT;
        LOG("path==\\0");
        return nullptr;
    }
    /* Make a copy of the source path since we may need to modify it. */
    path_len = strlen(path);
    if (path_len >= PATH_MAX - 2) {
        errno = ENAMETOOLONG;
        LOG("path_len>=PATH_MAX");
        return nullptr;
    }
    /* Copy so that path is at the end of copy_path[] */
    strcpy(copy_path + (PATH_MAX - 1) - path_len, path);
    path = copy_path + (PATH_MAX - 1) - path_len;
    allocated_path = resolved ? nullptr : (resolved = new char[PATH_MAX]);
    max_path = resolved + PATH_MAX - 2; /* points to last non-NUL char */
    new_path = resolved;
    if (*path != '/') {
        /* If it's a relative pathname use getcwd for starters. */
        capio_syscall(SYS_getcwd, new_path, PATH_MAX);
        new_path += strlen(new_path);
        if (new_path[-1] != '/')
            *new_path++ = '/';
    } else {
        *new_path++ = '/';
        path++;
    }
    /* Expand each slash-separated pathname component. */
    while (*path != '\0') {
        /* Ignore stray "/". */
        if (*path == '/') {
            path++;
            continue;
        }
        if (*path == '.') {
            /* Ignore ".". */
            if (path[1] == '\0' || path[1] == '/') {
                path++;
                continue;
            }
            if (path[1] == '.') {
                if (path[2] == '\0' || path[2] == '/') {
                    path += 2;
                    /* Ignore ".." at root. */
                    if (new_path == resolved + 1)
                        continue;
                    /* Handle ".." by backing up. */
                    while ((--new_path)[-1] != '/');
                    continue;
                }
            }
        }
        /* Safely copy the next pathname component. */
        while (*path != '\0' && *path != '/') {
            if (new_path > max_path) {
                errno = ENAMETOOLONG;
                err:
                free(allocated_path);
                LOG("returned at label err");
                return nullptr;
            }
            *new_path++ = *path++;
        }
#ifdef S_IFLNK
        /* Protect against infinite loops. */
        if (readlinks++ > MAX_READLINKS) {
            errno = ELOOP;
            LOG("error readlinks++ > MAX_READLINKS");
            goto err;
        }
        path_len = strlen(path);
        /* See if last (so far) pathname component is a symlink. */
        *new_path = '\0';
        {
            int sv_errno = errno;

            link_len = capio_syscall(SYS_readlink, resolved, copy_path, PATH_MAX - 1);
            if (link_len < 0) {
                /* EINVAL means the file exists but isn't a symlink. */
                if (errno != EINVAL) {
                    LOG("link_len<0 && errno!=EINVAL");
                    goto err;
                }
            } else {
                /* Safe sex check. */
                if (path_len + link_len >= PATH_MAX - 2) {
                    errno = ENAMETOOLONG;
                    LOG("error ENAMETOOLONG");
                    goto err;
                }
                /* Note: readlink doesn't add the null byte. */
                /* copy_path[link_len] = '\0'; - we don't need it too */
                if (*copy_path == '/')
                    /* Start over for an absolute symlink. */
                    new_path = resolved;
                else
                    /* Otherwise back up over this component. */
                    while (*(--new_path) != '/');
                /* Prepend symlink contents to path. */
                memmove(copy_path + (PATH_MAX - 1) - link_len - path_len, copy_path, link_len);
                path = copy_path + (PATH_MAX - 1) - link_len - path_len;
            }
            errno = sv_errno;
        }
#endif                            /* S_IFLNK */
        *new_path++ = '/';
    }
    /* Delete trailing slash but don't whomp a lone slash. */
    if (new_path != resolved + 1 && new_path[-1] == '/')
        new_path--;
    /* Make sure it's null terminated. */
    *new_path = '\0';
    return resolved;
}

static inline bool is_capio_path(long tid, const std::string &path_to_check, const std::string &capio_dir) {
    START_LOG(tid, "call(%s, %s)", path_to_check.c_str(), capio_dir.c_str());

    return (std::mismatch(capio_dir.begin(), capio_dir.end(), path_to_check.begin()).first == capio_dir.end() &&
            capio_dir.size() != path_to_check.size());
}

const std::string *capio_posix_realpath(long tid,
                                        const std::string *pathname,
                                        const std::string *capio_dir,
                                        const std::string *current_dir) {

    START_LOG(tid, "call(path=%s, capio_dir=%s, current_dir=%s)", pathname->c_str(), capio_dir->c_str(),
              current_dir->c_str());
    char *posix_real_path = capio_realpath((char *) pathname->c_str(), nullptr);

    //if capio_realpath fails, then it should be a capio_file
    if (posix_real_path == nullptr) {
        LOG("path is null due to errno='%s'", strerror(errno));

        if (current_dir->find(*capio_dir) != std::string::npos) {
            if (pathname[0] != "/") {
                auto newPath = new std::string(*capio_dir + "/" + *pathname);

                //remove /./ from path
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
            //if file not found, then error is returned
            LOG("Fatal: file %s is not a posix file, nor a capio file!", pathname->c_str());
            exit(EXIT_FAILURE);
        }
    }

    //if not, then check for realpath trough libc implementation
    LOG("Computed realpath = %s", posix_real_path);
    return new std::string(posix_real_path);
}

#endif // CAPIO_COMMON_FILESYSTEM_HPP
