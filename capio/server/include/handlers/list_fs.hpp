#ifndef CAPIO_SERVER_HANDLERS_LIST_FS_HPP
#define CAPIO_SERVER_HANDLERS_LIST_FS_HPP

#include "common/requests.hpp"
#include "handlers/fs_request.hpp"

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
    auto &state                      = FsStreaming::get();

    const bool is_starting = n_seen < 0;
    if (is_starting) {
        if (engine.getCommitRule(path) != capiocl::commitRules::ON_N_FILES ||
            !FsStreaming::canWaitForCommit(path) || engine.isExcluded(path) ||
            state.isProducer(tid, path)) {
            client_manager->replyToClient(tid, CAPIO_FS_LIST_AS_IS);
            return;
        }
        if (engine.isFirable(path)) {
            client_manager->replyToClient(tid, CAPIO_FS_LIST_STREAMING);
            return;
        }
    }
    const long expected = state.expectedEntries(path);
    fs_reply_when(tid, [path, n_seen, expected, is_starting](off64_t &reply) {
        const long count = FsStreaming::countEntries(path);
        reply            = CAPIO_FS_LIST_COMMITTED;
        if (count < 0 || count >= expected) {
            // removed, or committed by its entries
            return count < 0 || FsStreaming::isDirectoryCommitted(path, expected);
        }
        if (!is_starting && count != n_seen) {
            reply = CAPIO_FS_LIST_STREAMING;
            return true;
        }
        return CapioCLEngine::get().isCommitted(path);
    });
}

#endif // CAPIO_SERVER_HANDLERS_LIST_FS_HPP
