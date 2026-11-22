#ifndef CAPIO_SERVER_HANDLERS_READ_FS_HPP
#define CAPIO_SERVER_HANDLERS_READ_FS_HPP

#include "handlers/fs_request.hpp"

void read_fs_handler(const char *const str) {
    int tid, fd, consumed = 0;
    off64_t end_of_read;
    if (sscanf(str, "%d %d %ld%n", &tid, &fd, &end_of_read, &consumed) != 3) {
        return;
    }
    const std::filesystem::path path = fs_request_path(tid, str, consumed);

    // -1: there will never be anything to wait for, so the client does not ask again
    if (std::error_code error; std::filesystem::is_directory(path, error) ||
                               CapioCLEngine::get().isExcluded(path) ||
                               FsStreaming::get().isProducer(tid, path)) {
        client_manager->replyToClient(tid, -1);
        return;
    }

    const auto enough_data = [path, end_of_read](off64_t &readable) {
        if (!CapioCLEngine::get().isFirable(path)) {
            return false;
        }
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size < static_cast<uintmax_t>(end_of_read)) {
            return false;
        }
        readable = static_cast<off64_t>(size);
        return true;
    };

    if (off64_t readable; enough_data(readable)) {
        client_manager->replyToClient(tid, readable);
        return;
    }
    fs_reply_when(tid, [path, enough_data](off64_t &reply) {
        if (enough_data(reply)) {
            return true;
        }
        reply = -1;
        return CapioCLEngine::get().isCommitted(path);
    });
}

#endif // CAPIO_SERVER_HANDLERS_READ_FS_HPP
