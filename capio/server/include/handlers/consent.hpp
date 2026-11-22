#ifndef CAPIO_SERVER_HANDLERS_CONSENT_HPP
#define CAPIO_SERVER_HANDLERS_CONSENT_HPP

#include <chrono>
#include <thread>

#include "handlers/fs_request.hpp"

extern ClientManager *client_manager;

void consent_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) != 1) {
        return;
    }
    const std::string path = fs_request_path(tid, str, consumed);

    if (!CapioCLEngine::get().contains(path) || CapioCLEngine::get().isExcluded(path) ||
        FsStreaming::get().isProducer(tid, path)) {
        client_manager->replyToClient(tid, 1);
    } else if (!std::filesystem::exists(path)) {
        client_manager->spawnWaiter(
            tid, [path = std::string(path)](const ClientManager::ClientToken &client) {
                while (client_manager->isClientActive(client)) {
                    std::error_code error;
                    if (std::filesystem::exists(path, error) && !error) {
                        client_manager->tryReply(client, 1);
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
    } else if (!FsStreaming::canWaitForCommit(path) || CapioCLEngine::get().isFirable(path) ||
               CapioCLEngine::get().isCommitted(path)) {
        client_manager->replyToClient(tid, 1);
    } else {
        client_manager->spawnWaiter(
            tid, [path = std::string(path)](const ClientManager::ClientToken &client) {
                while (client_manager->isClientActive(client)) {
                    try {
                        if (CapioCLEngine::get().isCommitted(path)) {
                            client_manager->tryReply(client, 1);
                            return;
                        }
                    } catch (const std::filesystem::filesystem_error &) {
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
    }
}

#endif // CAPIO_SERVER_HANDLERS_CONSENT_HPP
