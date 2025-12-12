#ifndef CAPIO_SERVER_HANDLERS_STAT_HPP
#define CAPIO_SERVER_HANDLERS_STAT_HPP

#include <mutex>
#include <thread>

#include "remote/backend.hpp"

#include "remote/requests.hpp"

#include "utils/location.hpp"
#include "utils/types.hpp"

#include "client-manager/client_manager.hpp"
#include "storage/storage_service.hpp"

extern StorageService *storage_service;
extern ClientManager *client_manager;

void wait_for_file_completion(int tid, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());

    loop_load_file_location(path);
    // check if the file is local or remote
    CapioFile &c_file = storage_service->getCapioFile(path).value();

    // if file is streamable
    if (c_file.is_complete() || CapioCLEngine::get().isFirable(path) ||
        strcmp(std::get<0>(get_file_location(path)), node_name) == 0) {

        client_manager->replyToClient(tid, c_file.get_file_size());
        client_manager->replyToClient(tid, static_cast<int>(c_file.is_dir() ? 1 : 0));

    } else {
        handle_remote_stat_request(tid, path);
    }
}

inline void reply_stat(int tid, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());

    auto file_location_opt = get_file_location_opt(path);
    LOG("File %s is local? %s", path.c_str(), file_location_opt ? "True" : "False");

    if (!file_location_opt) {
        if (!load_file_location(path)) {
            LOG("path %s is not present in any node", path.c_str());
            // if it is in configuration file then wait otherwise fail

            if (const std::string &app_name = client_manager->getAppName(tid);
                CapioCLEngine::get().isProducer(path, app_name)) {
                LOG("Metadata do not contains file or globs did not contain file or app is "
                    "producer.");
                client_manager->replyToClient(tid, -1); // return size
                client_manager->replyToClient(tid, -1);
            } else {
                LOG("File found but not ready yet. Starting a thread to wait for file %s",
                    path.c_str());
                std::thread t(wait_for_file_completion, tid, std::filesystem::path(path));
                t.detach();
            }
            return;
        }
    }
    auto c_file_opt   = storage_service->getCapioFile(path);
    CapioFile &c_file = (c_file_opt) ? c_file_opt->get()
                                     : storage_service->add(path, false, get_file_initial_size());
    LOG("Obtained capio file. ready to reply to client");
    const std::filesystem::path &capio_dir = get_capio_dir();
    LOG("Obtained capio_dir");
    if (!file_location_opt) {
        LOG("File is now present from remote node. retrieving file again.");
        file_location_opt = get_file_location_opt(path);
    }
    if (c_file.is_complete() || strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 ||
        CapioCLEngine::get().isFirable(path) || capio_dir == path) {
        LOG("Sending response to client");
        client_manager->replyToClient(tid, c_file.get_file_size());
        client_manager->replyToClient(tid, static_cast<int>(c_file.is_dir() ? 1 : 0));
    } else {
        LOG("Delegating backend to reply to remote stats");
        // send a request for file. then start a thread to wait for the request completion
        c_file.create_buffer_if_needed(path, false);
        handle_remote_stat_request(tid, path);
    }
}

void fstat_handler(const char *const str) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);

    reply_stat(tid, storage_service->getFilePath(tid, fd));
}

void stat_handler(const char *const str) {
    char path[2048];
    int tid;
    sscanf(str, "%d %s", &tid, path);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    reply_stat(tid, path);
}

#endif // CAPIO_SERVER_HANDLERS_STAT_HPP
