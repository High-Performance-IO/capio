#ifndef SRC_CAPIO_POSIX_GLOBALS_H
#define SRC_CAPIO_POSIX_GLOBALS_H

#include <filesystem>
#include <set>
#include <string>

#include <libsyscall_intercept_hook_point.h>
#include <semaphore.h>
#include <syscall.h>

#include "capio/logger.hpp"
#include "capio/constants.hpp"
#include "capio/filesystem.hpp"

#include "utils/env.hpp"
#include "utils/requests.hpp"
#include "utils/snapshot.hpp"
#include "utils/types.hpp"

const std::string *current_dir = nullptr;

CPFileDescriptors_t *capio_files_descriptors = nullptr;
CPFilesPaths_t *capio_files_paths = nullptr;
CPFiles_t *files = nullptr;

CPThreadDataBufs_t *threads_data_bufs = nullptr;

/* Allows CAPIO to deactivate syscalls hooking. */
thread_local bool syscall_no_intercept_flag = false;


void mtrace_init(long tid) {

    START_LOG(tid, "call()");

    syscall_no_intercept_flag = true;

    if (capio_files_descriptors == nullptr) {
        capio_files_descriptors = new CPFileDescriptors_t();
        capio_files_paths = new CPFilesPaths_t();
        files = new CPFiles_t();

        int *fd_shm = get_fd_snapshot(tid);
        if (fd_shm != nullptr) {
            initialize_from_snapshot(fd_shm, files, capio_files_descriptors, capio_files_paths, tid);
        }
        threads_data_bufs = new CPThreadDataBufs_t;
        auto *write_queue = new SPSC_queue<char>(
                "capio_write_data_buffer_tid_" + std::to_string(tid),
                N_ELEMS_DATA_BUFS,
                WINDOW_DATA_BUFS,
                CAPIO_SEM_TIMEOUT_NANOSEC,
                CAPIO_SEM_RETRIES);
        auto *read_queue = new SPSC_queue<char>(
                "capio_read_data_buffer_tid_" + std::to_string(tid),
                N_ELEMS_DATA_BUFS,
                WINDOW_DATA_BUFS,
                CAPIO_SEM_TIMEOUT_NANOSEC,
                CAPIO_SEM_RETRIES);
        threads_data_bufs->insert({static_cast<int>(tid), {write_queue, read_queue}});
    }

    register_listener(tid);

    const char *capio_app_name = get_capio_app_name();
    long pid = syscall_no_intercept(SYS_getpid);
    if (capio_app_name == nullptr)
        handshake_anonymous_request(tid, pid);
    else
        handshake_named_request(tid, pid, capio_app_name);

    syscall_no_intercept_flag = false;
}

#endif // SRC_CAPIO_POSIX_GLOBALS_H
