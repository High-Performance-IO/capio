#ifndef CAPIO_POSIX_UTILS_SNAPSHOT_HPP
#define CAPIO_POSIX_UTILS_SNAPSHOT_HPP

#include <climits>
#include <string>
#include <tuple>

#include "capio/logger.hpp"

#include "types.hpp"

/**
 * EXPLANATION OF SNAPSHOT.
 * From the execve manual: By default, file descriptors remain open across an execve().
 * File descriptors that are marked close-on-exec are closed;
 *
 * This means that the snapshot is used to move file descriptor metadata (that is handled by capio)
 * forward to the new process through shared memory. at the execve syscall, a snapshot is created,
 * by storing the required information and then it gets inherited by the new process, removing the
 * FDs opened with FD_CLOEXEC
 */

inline int *get_fd_snapshot(long tid) {
#ifdef __CAPIO_POSIX
    syscall_no_intercept_flag = true;
#endif
    const auto return_value =
        static_cast<int *>(get_shm_if_exist("capio_snapshot_" + std::to_string(tid)));
#ifdef __CAPIO_POSIX
    syscall_no_intercept_flag = false;
#endif
    return return_value;
}

inline void initialize_from_snapshot(const int *fd_shm, pid_t tid) {
    START_LOG(tid, "call(%ld)", fd_shm);
    int i = 0;
    std::string shm_name;
    int fd;
    capio_off64_t *p_shm;
    char *path_shm;

    std::string pid = std::to_string(tid);
#ifdef __CAPIO_POSIX
    syscall_no_intercept_flag = true;
#endif
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
        p_shm    = static_cast<capio_off64_t *>(get_shm(shm_name));
        add_capio_fd(tid, path_shm, fd, p_shm[1], static_cast<bool>(p_shm[2]));

        if (munmap(p_shm, 3 * sizeof(capio_off64_t)) == -1) {
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
#ifdef __CAPIO_POSIX
    syscall_no_intercept_flag = false;
#endif
}

inline void create_snapshot(long tid) {
    START_LOG(tid, "call()");
    capio_off64_t *p_shm;
    char *path_shm;

    std::string pid      = std::to_string(tid);
    std::vector<int> fds = get_capio_fds();
    if (fds.empty()) {
        return;
    }
#ifdef __CAPIO_POSIX
    syscall_no_intercept_flag = true;
#endif
    int *fd_shm =
        static_cast<int *>(create_shm("capio_snapshot_" + pid, (fds.size() + 1) * sizeof(int)));
    int i = 0;

    for (auto &fd : fds) {
        fd_shm[i] = fd;
        p_shm     = (capio_off64_t *) create_shm("capio_snapshot_" + pid + "_" + std::to_string(fd),
                                                 3 * sizeof(capio_off64_t));
        p_shm[0]  = fd;
        p_shm[1]  = get_capio_fd_offset(fd);
        p_shm[2]  = get_capio_fd_cloexec(fd);

        std::string shm_name = "capio_snapshot_path_" + pid + "_" + std::to_string(fd);
        path_shm             = (char *) create_shm(shm_name, PATH_MAX * sizeof(char));
        strcpy(path_shm, get_capio_fd_path(fd).c_str());
        ++i;
    }
    fd_shm[i] = -1;
#ifdef __CAPIO_POSIX
    syscall_no_intercept_flag = false;
#endif
}

#endif // CAPIO_POSIX_UTILS_SNAPSHOT_HPP