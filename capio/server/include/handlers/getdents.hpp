#ifndef CAPIO_GETDENTS_HPP
#define CAPIO_GETDENTS_HPP

#include <chrono>
#include <csignal>
#include <pthread.h>
#include <thread>

#include "common/requests.hpp"
#include "remote/backend.hpp"
#include "remote/home_node_service.hpp"
#include "remote/requests.hpp"
#include "utils/common.hpp"
#include "utils/runtime_configuration.hpp"

extern StorageManager *storage_manager;
extern Backend *backend;

/// @brief Serve a getdents of a directory whose home is @p home, another server, from what this
/// server has of it, or ask @p home for the entries
inline void request_remote_getdents(int tid, int fd, off64_t count, const std::string &home) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, home=%s)", tid, fd, count, home.c_str());

    CapioFile &c_file     = storage_manager->get(tid, fd);
    off64_t offset        = storage_manager->getFileOffset(tid, fd);
    off64_t end_of_read   = offset + count;
    off64_t end_of_sector = c_file.getSectorEnd(offset);

    if (c_file.isCommitted() && (end_of_read <= end_of_sector ||
                                 (end_of_sector == CAPIO_SECTOR_NOT_FOUND ? 0 : end_of_sector) ==
                                     c_file.getRealFileSize())) {
        LOG("Handling local read");
        send_dirent_to_client(tid, fd, c_file, offset, count);
    } else if (end_of_read <= end_of_sector) {
        LOG("?");
        c_file.createBuffer(storage_manager->getPath(tid, fd), false);
        client_manager->replyToClient(tid, offset, c_file.getBuffer(), count);
        storage_manager->setFileOffset(tid, fd, offset + count);
    } else {
        LOG("Delegating to backend remote read");
        handle_remote_read_request(tid, fd, count, true, home);
    }
}

inline void handle_getdents(int tid, int fd, long int count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    const std::string &app_name                = client_manager->getAppName(tid);
    const std::filesystem::path &path_to_check = storage_manager->getPath(tid, fd);
    const std::filesystem::path &capio_dir     = RuntimeConfiguration::get().getCapioDir();
    bool is_prod           = CapioCLEngine::get().isProducer(path_to_check, app_name);
    auto file_location_opt = home_node_service->getHome(path_to_check);

    if (!file_location_opt && !is_prod) {
        std::thread t([tid, fd, count, path_to_check] {
            START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

            if (const auto home = home_node_service->waitForHome(path_to_check);
                home == backend->get_node_name()) {
                handle_getdents(tid, fd, count);
            } else {
                request_remote_getdents(tid, fd, count, home);
            }
        });
        t.detach();
    } else if (is_prod || *file_location_opt == backend->get_node_name() ||
               capio_dir == path_to_check) {
        CapioFile &c_file = storage_manager->get(path_to_check);
        off64_t offset    = storage_manager->getFileOffset(tid, fd);
        send_dirent_to_client(tid, fd, c_file, offset, count);
    } else {
        LOG("File is remote. Delegating to backend remote read");
        request_remote_getdents(tid, fd, count, *file_location_opt);
    }
}

void getdents_handler(const char *const str) {
    int tid, fd;
    off64_t count;
    sscanf(str, "%d %d %ld", &tid, &fd, &count);
    handle_getdents(tid, fd, count);
}

/// @brief Reply to @p tid once the listing of @p path can go on: new entries, or the commit
inline void wait_for_fs_entries(const std::filesystem::path path, const pid_t tid,
                                const off64_t n_seen, const long expected, const bool is_starting) {
    for (;;) {
        const long count = FsStorageManager::countEntries(path);
        if (count < 0 || count >= expected) {
            // removed, or committed by its entries
            if (count < 0 || FsStorageManager::isDirectoryCommitted(path, expected)) {
                client_manager->replyToClient(tid, CAPIO_FS_LIST_COMMITTED);
                return;
            }
        } else if (!is_starting && count != n_seen) {
            client_manager->replyToClient(tid, CAPIO_FS_LIST_STREAMING);
            return;
        } else if (CapioCLEngine::get().isCommitted(path)) {
            client_manager->replyToClient(tid, CAPIO_FS_LIST_COMMITTED);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

/*
 * getdents of the FS variant, see posix/utils/fs_listing.hpp: "tid n_seen path". With n_seen -1
 * the listing starts. Otherwise it found nothing new among n_seen entries, and waits.
 */
void list_fs_handler(const char *const str) {
    int tid, consumed = 0;
    off64_t n_seen;
    if (sscanf(str, "%d %ld%n", &tid, &n_seen, &consumed) != 2) {
        return;
    }
    const std::filesystem::path path = fs_request_path(tid, str, consumed);
    const auto &engine               = CapioCLEngine::get();

    const bool is_starting = n_seen < 0;
    if (is_starting) {
        if (engine.getCommitRule(path) != capiocl::commitRules::ON_N_FILES ||
            engine.isExcluded(path) || FsStorageManager::isProducer(tid, path)) {
            client_manager->replyToClient(tid, CAPIO_FS_LIST_AS_IS);
            return;
        }
        if (engine.isFirable(path)) {
            client_manager->replyToClient(tid, CAPIO_FS_LIST_STREAMING);
            return;
        }
    }
    const long expected = fs_storage_manager->expectedEntries(path);
    // The waiter does not get the termination signals: their handler frees memory and exits, and
    // run on a thread that holds a lock (malloc's, CAPIO-CL's) it would wait for it forever.
    // TODO: a thread manager for the threads of the server
    sigset_t signals, previous;
    sigemptyset(&signals);
    for (const int signal : {SIGTERM, SIGINT, SIGQUIT, SIGPIPE}) {
        sigaddset(&signals, signal);
    }
    pthread_sigmask(SIG_BLOCK, &signals, &previous);
    std::thread t(wait_for_fs_entries, path, tid, n_seen, expected, is_starting);
    t.detach();
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
}

#endif // CAPIO_GETDENTS_HPP
