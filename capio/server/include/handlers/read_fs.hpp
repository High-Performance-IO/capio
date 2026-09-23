#ifndef CAPIO_SERVER_HANDLERS_READ_FS_HPP
#define CAPIO_SERVER_HANDLERS_READ_FS_HPP

#include <chrono>
#include <thread>

void read_fs_handler(const char *const str) {
    long tid;
    int fd;
    off64_t end_of_read;
    char path[PATH_MAX];
    if (sscanf(str, "%ld %d %s %ld", &tid, &fd, path, &end_of_read) != 4) {
        return;
    }

    const bool is_producer = CapioCLEngine::get().isProducer(
                                 path, client_manager->getAppName(tid)) ||
                             client_manager->isProducer(tid, path);
    const auto reply_if_ready = [tid, end_of_read, is_producer, path = std::string(path)](
                                    const ClientManager::ClientToken *client) {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error || !exists || std::filesystem::is_directory(path, error) || error) {
            return false;
        }
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            return false;
        }
        const bool ready = is_producer || (CapioCLEngine::get().isFirable(path) &&
                                           size >= static_cast<uintmax_t>(end_of_read));
        if (ready) {
            return client == nullptr
                       ? (client_manager->replyToClient(tid, static_cast<off64_t>(size)), true)
                       : client_manager->tryReply(*client, static_cast<off64_t>(size));
        }
        try {
            if (CapioCLEngine::get().isCommitted(path)) {
                return client == nullptr ? (client_manager->replyToClient(tid, -1), true)
                                         : client_manager->tryReply(*client, -1);
            }
        } catch (const std::filesystem::filesystem_error &) {
        }
        return false;
    };

    if (reply_if_ready(nullptr)) {
        return;
    }
    client_manager->spawnWaiter(
        tid, [reply_if_ready](const ClientManager::ClientToken &client) {
            while (client_manager->isClientActive(client)) {
                try {
                    if (reply_if_ready(&client)) {
                        return;
                    }
                } catch (const std::system_error &) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
}

#endif // CAPIO_SERVER_HANDLERS_READ_FS_HPP
