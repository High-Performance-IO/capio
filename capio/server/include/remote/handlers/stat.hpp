#ifndef CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP

#include "calf/StlLogger.h"
#include "client-manager/client_manager.hpp"
#include "remote/backend.hpp"
#include "remote/requests.hpp"
#include "storage/manager.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;

inline void serve_remote_stat(const std::filesystem::path &path, const std::string &dest,
                              int source_tid) {
    START_LOG(gettid(), "call(path=%s, dest=%s, source_tid%d)", path.c_str(), dest.c_str(),
              source_tid);

    const CapioFile &c_file = storage_manager->get(path);
    off64_t file_size       = c_file.getFileSize();
    bool is_dir             = c_file.isDirectory();
    serve_remote_stat_request(path, source_tid, file_size, is_dir, dest);
}

inline void wait_for_completion(const std::filesystem::path &path, int source_tid,
                                const std::string &dest) {
    START_LOG(gettid(), "call(path=%s, source_tid=%d, dest=%s)", path.c_str(), source_tid,
              dest.c_str());

    const CapioFile &c_file = storage_manager->get(path);
    c_file.waitForCommit();
    LOG("File %s has been completed. serving stats data", path.c_str());
    serve_remote_stat(path, dest, source_tid);
}

inline void handle_remote_stat(int source_tid, const std::filesystem::path &path,
                               const std::string &dest) {
    START_LOG(gettid(), "call(source_tid=%d, path=%s, dest=%s)", source_tid, path.c_str(),
              dest.c_str());

    const auto c_file = storage_manager->tryGet(path);
    if (c_file) {
        LOG("File %s is present on capio file system", path.c_str());
        if (c_file->get().isCommitted() || CapioCLEngine::get().isFirable(path)) {
            LOG("file is complete. serving file");
            serve_remote_stat(path, dest, source_tid);
        } else { // wait for completion
            LOG("File is not complete. awaiting completion on different thread. parameters of wait "
                "are: path=%s, source_tid=%d, dest=%s",
                path.c_str(), source_tid, dest.c_str());
            std::thread t(wait_for_completion, path, source_tid, dest);
            t.detach();
        }
    } else {
        LOG("CAPIO file is not in metadata. checking in globs for files to be created");
        LOG("File is in globs. creating capio_file and starting thread awaiting for future "
            "creation of file");
        storage_manager->addFile(path);
        std::thread t(wait_for_completion, path, source_tid, dest);
        t.detach();
    }
}

inline void handle_remote_stat_reply(const std::filesystem::path &path, int source_tid,
                                     off64_t size, bool dir) {
    START_LOG(gettid(), "call(path=%s, source_tid=%d, size=%ld, dir=%s)", path.c_str(), source_tid,
              size, dir ? "true" : "false");

    client_manager->replyToClient(source_tid, size);
    client_manager->replyToClient(source_tid, static_cast<off64_t>(dir));
}

/// "<tid> <path>"
inline void remote_stat_handler(const RemoteRequest &request) {
    int tid, path_start = 0;
    if (sscanf(request.get_content(), "%d %n", &tid, &path_start) < 1 || path_start == 0) {
        return;
    }
    handle_remote_stat(tid, request.get_content() + path_start, request.get_source());
}

/// "<tid> <size> <1 if a directory> <path>"
inline void remote_stat_reply_handler(const RemoteRequest &request) {
    off64_t size;
    int dir, source_tid, path_start = 0;
    if (sscanf(request.get_content(), "%d %ld %d %n", &source_tid, &size, &dir, &path_start) < 3 ||
        path_start == 0) {
        return;
    }
    handle_remote_stat_reply(request.get_content() + path_start, source_tid, size, dir);
}

#endif // CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP
