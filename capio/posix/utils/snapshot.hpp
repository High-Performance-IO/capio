#ifndef CAPIO_POSIX_UTILS_SNAPSHOT_HPP
#define CAPIO_POSIX_UTILS_SNAPSHOT_HPP

#include <climits>
#include <string>
#include <tuple>

#include "common/logger.hpp"

#include "types.hpp"

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

#endif // CAPIO_POSIX_UTILS_SNAPSHOT_HPP
