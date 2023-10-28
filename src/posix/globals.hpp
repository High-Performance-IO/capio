#ifndef CAPIO_POSIX_GLOBALS_HPP
#define CAPIO_POSIX_GLOBALS_HPP

#include <semaphore.h>

#include <filesystem>
#include <set>
#include <string>

#include "capio/constants.hpp"
#include "capio/filesystem.hpp"
#include "capio/logger.hpp"
#include "capio/syscall.hpp"

#include "utils/data.hpp"
#include "utils/env.hpp"
#include "utils/requests.hpp"
#include "utils/snapshot.hpp"
#include "utils/types.hpp"

const std::string *current_dir = nullptr;

CPFileDescriptors_t *capio_files_descriptors = nullptr;
CPFilesPaths_t *capio_files_paths            = nullptr;
CPFiles_t *files                             = nullptr;

void mtrace_init(long tid) {
    START_LOG(syscall_no_intercept(SYS_gettid), "call(tid=%ld)", tid);

    syscall_no_intercept_flag = true;

    if (capio_files_descriptors == nullptr) {
        capio_files_descriptors = new CPFileDescriptors_t();
        capio_files_paths       = new CPFilesPaths_t();
        files                   = new CPFiles_t();

        int *fd_shm = get_fd_snapshot(tid);
        if (fd_shm != nullptr) {
            initialize_from_snapshot(fd_shm, files, capio_files_descriptors, capio_files_paths,
                                     tid);
        }
    }

    register_listener(tid);
    register_data_listener(tid);

    const char *capio_app_name = get_capio_app_name();
    long pid                   = syscall_no_intercept(SYS_getpid);
    if (capio_app_name == nullptr) {
        handshake_anonymous_request(tid, pid);
    } else {
        handshake_named_request(tid, pid, capio_app_name);
    }

    syscall_no_intercept_flag = false;
}

#endif // CAPIO_POSIX_GLOBALS_HPP
