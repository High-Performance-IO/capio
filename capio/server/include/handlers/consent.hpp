#ifndef CAPIO_SERVER_HANDLERS_CONSENT_HPP
#define CAPIO_SERVER_HANDLERS_CONSENT_HPP

#include <chrono>
#include <thread>

extern ClientManager *client_manager;

void consent_handler(const char *const str) {
    long tid;
    char path[PATH_MAX], source[PATH_MAX];
    if (sscanf(str, "%ld %s %s", &tid, path, source) != 3) {
        return;
    }

    if (!CapioCLEngine::get().contains(path) || CapioCLEngine::get().isExcluded(path) ||
        CapioCLEngine::get().isProducer(path, client_manager->getAppName(tid)) ||
        client_manager->isProducer(tid, path)) {
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
    } else if (CapioCLEngine::get().isFirable(path) || CapioCLEngine::get().isCommitted(path)) {
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
