#ifndef CAPIO_SERVER_HANDLERS_CLOSE_HPP
#define CAPIO_SERVER_HANDLERS_CLOSE_HPP

#include "handlers/fs_request.hpp"
#include "read.hpp"

extern StorageManager *storage_manager;

inline void handle_close(int tid, int fd) {
    START_LOG(gettid(), "call(tid=%d, fd=%d)", tid, fd);

    const std::filesystem::path path = storage_manager->getPath(tid, fd);
    if (path.empty()) { // avoid to try to close a file that does not exists
        // (example: try to close() on a dir
        LOG("Path is empty. might be a directory. returning");
        return;
    }

    CapioFile &c_file = storage_manager->get(tid, fd);
    c_file.close();
    LOG("File was closed", path.c_str());

    if (CapioCLEngine::get().getCommitRule(path) == capiocl::commitRules::ON_CLOSE &&
        c_file.closed()) {
        LOG("Capio File %s is closed and commit rule is on_close. setting it to complete",
            path.c_str());
        c_file.setCommitted();
        c_file.dump();
    }

    LOG("Deleting capio file %s from tid=%d", path.c_str(), tid);
    storage_manager->removeFromTid(tid, fd);
}

inline void handle_close_fs(const int tid, const std::filesystem::path &path) {
    if (CapioCLEngine::get().getCommitRule(path) == capiocl::commitRules::ON_CLOSE &&
        FsStreaming::get().isProducer(tid, path)) {
        CapioCLEngine::get().increaseCloseCount(path);
    }
}

void close_handler(const char *str) {
    int tid, fd, consumed = 0;
    sscanf(str, "%d %d%n", &tid, &fd, &consumed);
    if (fd == -1) {
        handle_close_fs(tid, fs_request_path(tid, str, consumed));
        return;
    }
    handle_close(tid, fd);
}

#endif // CAPIO_SERVER_HANDLERS_CLOSE_HPP
