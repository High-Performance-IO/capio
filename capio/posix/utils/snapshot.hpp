#ifndef CAPIO_POSIX_UTILS_SNAPSHOT_HPP
#define CAPIO_POSIX_UTILS_SNAPSHOT_HPP

#include <climits>
#include <string>
#include <tuple>

#include "calf/SyscallLogger.h"
#include "types.hpp"

#if !CAPIO_MEM_STREAM
#include <filesystem>
#include <sys/stat.h>

#include "filesystem.hpp"
#include "requests.hpp"
#endif

inline int *get_fd_snapshot(long tid) {
    return static_cast<int *>(get_shm_if_exist("capio_snapshot_" + std::to_string(tid)));
}

void initialize_from_snapshot(const int *fd_shm, long tid) {
    START_LOG(tid, "call(%ld)", fd_shm);
    int i = 0;
    std::string shm_name;
    int fd;
    off64_t *p_shm;
    char *path_shm;

    std::string pid = std::to_string(tid);

    while ((fd = fd_shm[i]) != -1) {
        shm_name = "capio_snapshot_path_" + pid + "_" + std::to_string(fd);
        path_shm = static_cast<char *>(get_shm(shm_name));
        if (munmap(path_shm, PATH_MAX) == -1) {
            ERR_EXIT("munmap initialize_from_snapshot");
        }
        if (shm_unlink(shm_name.c_str()) == -1) {
            ERR_EXIT("shm_unlink snapshot %s", shm_name.c_str());
        }
        shm_name = "capio_snapshot_" + pid + "_" + std::to_string(fd);
        p_shm    = static_cast<off64_t *>(get_shm(shm_name));
        add_capio_fd(tid, path_shm, fd, p_shm[1], p_shm[2], static_cast<int>(p_shm[3]),
                     static_cast<bool>(p_shm[4]));
        if (munmap(p_shm, 6 * sizeof(off64_t)) == -1) {
            ERR_EXIT("munmap 2 initialize_from_snapshot");
        }
        if (shm_unlink(shm_name.c_str()) == -1) {
            ERR_EXIT("shm_unlink snapshot %s", shm_name.c_str());
        }
        ++i;
    }
    shm_name = "capio_snapshot_" + pid;
    if (shm_unlink(shm_name.c_str()) == -1) {
        ERR_EXIT("shm_unlink snapshot %s", shm_name.c_str());
    }
}

void create_snapshot(long tid) {
    START_LOG(tid, "call()");
    off64_t *p_shm;
    char *path_shm;

    std::string pid      = std::to_string(tid);
    std::vector<int> fds = get_capio_fds();
    if (fds.empty()) {
        return;
    }

    int *fd_shm =
        static_cast<int *>(create_shm("capio_snapshot_" + pid, (fds.size() + 1) * sizeof(int)));
    int i = 0;

    for (auto &fd : fds) {
        fd_shm[i] = fd;
        p_shm     = (off64_t *) create_shm("capio_snapshot_" + pid + "_" + std::to_string(fd),
                                           5 * sizeof(off64_t));
        p_shm[0]  = fd;
        p_shm[1]  = get_capio_fd_offset(fd);
        p_shm[2]  = get_capio_fd_size(fd);
        p_shm[3]  = get_capio_fd_flags(fd);
        p_shm[4]  = get_capio_fd_cloexec(fd);

        std::string shm_name = "capio_snapshot_path_" + pid + "_" + std::to_string(fd);
        path_shm             = (char *) create_shm(shm_name, PATH_MAX * sizeof(char));
        strcpy(path_shm, get_capio_fd_path(fd).c_str());
        ++i;
    }
    fd_shm[i] = -1;
}

#if !CAPIO_MEM_STREAM
/**
 * FS variant: the descriptors of CAPIO files that a program inherits are real ones, so they are
 * found in /proc/self/fd instead of in a snapshot. That also finds the ones opened outside CAPIO
 * (a posix_spawn file action, a shell that is not preloaded): if nobody registered as the
 * producer of a writable one, the program that writes it is.
 */
void initialize_from_inherited_fds(long tid) {
    START_LOG(tid, "call()");
    if (std::getenv("CAPIO_DIR") == nullptr) {
        return;
    }
    syscall_no_intercept_flag = true;
    std::vector<int> open_fds;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd", error)) {
        open_fds.push_back(std::atoi(entry.path().filename().c_str()));
    }
    for (const int fd : open_fds) {
        struct stat info{};
        const long fd_flags = syscall_no_intercept(SYS_fcntl, fd, F_GETFD);
        if (fd_flags < 0 || syscall_no_intercept(SYS_fstat, fd, &info) != 0 || info.st_nlink == 0) {
            continue; // the descriptor of the listing itself, or a removed file
        }
        const auto path = get_dir_path(fd);
        if (path.empty() || !is_capio_path(path)) {
            continue;
        }
        const auto flags = static_cast<int>(syscall_no_intercept(SYS_fcntl, fd, F_GETFL));
        add_capio_fd(tid, path, fd, 0, CAPIO_DEFAULT_FILE_INITIAL_SIZE, flags,
                     (fd_flags & FD_CLOEXEC) != 0);
        if (!S_ISDIR(info.st_mode) && (flags & O_ACCMODE) != O_RDONLY) {
            inherit_request_fs(path, tid);
        }
    }
    syscall_no_intercept_flag = false;
}
#endif // !CAPIO_MEM_STREAM

#endif // CAPIO_POSIX_UTILS_SNAPSHOT_HPP
