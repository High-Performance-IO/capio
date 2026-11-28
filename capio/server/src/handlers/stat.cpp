#include "handlers.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>
#include <unistd.h>

#include "client-manager/client_manager.hpp"
#include "remote/backend.hpp"
#include "remote/home_node_service.hpp"
#include "remote/requests.hpp"
#include "storage/fs_storage_manager.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"
#include "utils/fs_root.hpp"
#include "utils/server_state.hpp"
#include "utils/thread_manager.hpp"
#include "utils/types.hpp"

void wait_for_file_completion(int tid, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());

    const auto home   = home_node_service->waitForHome(path);
    // check if the file is local or remote
    CapioFile &c_file = storage_manager->get(path);

    // if file is streamable
    if (c_file.isCommitted() || CapioCLEngine::get().isFirable(path) ||
        home == backend->get_node_name()) {

        client_manager->replyToClient(tid, c_file.getFileSize());
        client_manager->replyToClient(tid, static_cast<int>(c_file.isDirectory() ? 1 : 0));

    } else {
        handle_remote_stat_request(tid, path, home);
    }
}

void reply_stat(int tid, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());

    auto file_location_opt = home_node_service->getHome(path);
    LOG("File %s is local? %s", path.c_str(), file_location_opt ? "True" : "False");

    if (!file_location_opt) {
        file_location_opt = home_node_service->lookup(path);
        if (!file_location_opt) {
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
                thread_manager().startWaiter(thread_name("stat", path, tid, "its commit"),
                                             wait_for_file_completion, tid,
                                             std::filesystem::path(path));
            }
            return;
        }
    }
    auto c_file_opt   = storage_manager->tryGet(path);
    CapioFile &c_file = c_file_opt ? c_file_opt->get() : storage_manager->addFile(path);
    LOG("Obtained capio file. ready to reply to client");
    if (c_file.isCommitted() || *file_location_opt == backend->get_node_name() ||
        CapioCLEngine::get().isFirable(path)) {
        LOG("Sending response to client");
        client_manager->replyToClient(tid, c_file.getFileSize());
        client_manager->replyToClient(tid, static_cast<int>(c_file.isDirectory() ? 1 : 0));
    } else {
        LOG("Delegating backend to reply to remote stats");
        // send a request for file. then start a thread to wait for the request completion
        c_file.createBuffer(path, false);
        handle_remote_stat_request(tid, path, *file_location_opt);
    }
}

void fstat_handler(const char *const str) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);

    reply_stat(tid, storage_manager->getPath(tid, fd));
}

void stat_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) != 1) {
        return;
    }
    const std::string path = request_text(tid, str, consumed);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    reply_stat(tid, path);
}

void consent_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) != 1) {
        return;
    }
    const std::filesystem::path path = fs_request_path(tid, str, consumed);

    std::error_code error;
    const bool exists = std::filesystem::exists(fs_root().real(path), error);
    if (!exists && !FsStorageManager::waitsForCreation(tid, path)) {
        client_manager->replyToClient(tid, 0);
        return;
    }
    if (exists) {
        if (CapioCLEngine::get().isExcluded(path) || FsStorageManager::isProducer(tid, path)) {
            client_manager->replyToClient(tid, 1);
            return;
        }
        // Directories are never committed, except those with an on_n_files rule
        if ((std::filesystem::is_directory(fs_root().real(path), error) &&
             CapioCLEngine::get().getCommitRule(path) != capiocl::commitRules::ON_N_FILES) ||
            !FsStorageManager::isDeclared(path)) {
            client_manager->replyToClient(tid, 0);
            return;
        }
        if (CapioCLEngine::get().isFirable(path)) {
            client_manager->replyToClient(tid, 1);
            return;
        }
    }
    thread_manager().startWaiter(thread_name("stat", path, tid, "its consent"), [path, tid] {
        client_manager->replyToClient(tid, fs_storage_manager->waitForConsent(path));
    });
}
