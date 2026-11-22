#ifndef CAPIO_SERVER_HANDLERS_FS_REQUEST_HPP
#define CAPIO_SERVER_HANDLERS_FS_REQUEST_HPP

#include <chrono>
#include <functional>
#include <thread>

#include "client-manager/client_manager.hpp"
#include "fs-streaming/fs_streaming.hpp"

extern ClientManager *client_manager;

/*
 * Requests of the FS variant end with the path, which may contain spaces, so the handlers read the
 * arguments before it with sscanf and a final %n, and the path from there to the end.
 */

/// @brief The path of a request of the FS variant, whose arguments end at @p consumed
inline std::string fs_request_path(const pid_t tid, const char *str, const int consumed) {
    // %n follows the last number: one space separates it from the path
    auto path = std::filesystem::path(FsStreaming::get().takePath(
                                          tid, str[consumed] == ' ' ? str + consumed + 1
                                                                    : str + consumed))
                    .lexically_normal()
                    .string();
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

/**
 * Reply to @p tid from a waiter thread, once @p ready gives a value. The waiter may ask whether a
 * file is committed, which blocks, so it asks even the first time: never the request thread.
 *
 * @param ready returns whether the wait is over, and then sets the reply
 */
inline void fs_reply_when(const pid_t tid, std::function<bool(off64_t &)> ready) {
    client_manager->spawnWaiter(
        tid, [ready = std::move(ready)](const ClientManager::ClientToken &client) {
            while (client_manager->isClientActive(client)) {
                try {
                    off64_t reply = 0;
                    if (ready(reply)) {
                        client_manager->tryReply(client, reply);
                        return;
                    }
                } catch (const std::exception &exception) {
                    START_LOG(gettid(), "call()");
                    LOG("Will check again: %s", exception.what());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
}

void path_part_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) == 1 && str[consumed] == ' ') {
        FsStreaming::get().addPathPart(tid, str + consumed + 1);
    }
}

#endif // CAPIO_SERVER_HANDLERS_FS_REQUEST_HPP
