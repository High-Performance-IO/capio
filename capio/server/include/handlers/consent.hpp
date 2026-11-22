#ifndef CAPIO_SERVER_HANDLERS_CONSENT_HPP
#define CAPIO_SERVER_HANDLERS_CONSENT_HPP

#include "handlers/fs_request.hpp"

/*
 * stat, access, chdir: wait for the path to exist, as open does, and then, for a declared path in
 * update mode, for its commit. The reply is 1 if it holds from now on, so that the client can cache
 * it, and 0 if it only holds now (the path is missing, or is not declared).
 */
void consent_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) != 1) {
        return;
    }
    const std::filesystem::path path = fs_request_path(tid, str, consumed);

    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (!exists && !FsStreaming::get().waitsForCreation(tid, path)) {
        client_manager->replyToClient(tid, 0);
        return;
    }
    if (exists) {
        if (CapioCLEngine::get().isExcluded(path) || FsStreaming::get().isProducer(tid, path)) {
            client_manager->replyToClient(tid, 1);
            return;
        }
        if (!FsStreaming::canWaitForCommit(path) || !FsStreaming::isDeclared(path)) {
            client_manager->replyToClient(tid, 0);
            return;
        }
        if (CapioCLEngine::get().isFirable(path)) {
            client_manager->replyToClient(tid, 1);
            return;
        }
    }
    fs_reply_when(tid, [path](off64_t &reply) {
        std::error_code not_there;
        if (!std::filesystem::exists(path, not_there)) {
            return false;
        }
        if (!FsStreaming::canWaitForCommit(path) || !FsStreaming::isDeclared(path)) {
            return true;
        }
        reply = 1;
        return CapioCLEngine::get().isFirable(path) || CapioCLEngine::get().isCommitted(path);
    });
}

#endif // CAPIO_SERVER_HANDLERS_CONSENT_HPP
