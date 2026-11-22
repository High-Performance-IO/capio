#ifndef CAPIO_SERVER_HANDLERS_CLOSE_HPP
#define CAPIO_SERVER_HANDLERS_CLOSE_HPP

#include "common/requests.hpp"
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

void handle_exit_group(int tid);

inline void handle_close_fs(const int tid, const char *const str) {
    int fd, writable, error, consumed = 0;
    int ignored;
    char kind;
    if (sscanf(str, "%d %d %c %d %d%n", &ignored, &fd, &kind, &writable, &error, &consumed) != 5) {
        return;
    }
    const std::filesystem::path path = fs_request_path(tid, str, consumed);
    auto &state                      = FsStreaming::get();

    if (kind == CAPIO_FS_PROCESS_EXIT) {
        // exit_group ends the other threads, and none of them can say so
        for (const auto other : state.otherThreads(tid)) {
            handle_exit_group(other);
        }
    } else if (kind == CAPIO_FS_EXEC_FAILED) {
        state.execFailed(tid);
    } else if (error != 0) {
        state.closeFailed(tid, path, error);
    } else if (kind == CAPIO_FS_CLOSE_AT_EXEC) {
        state.closeAtExec(tid, path, writable != 0);
    } else {
        state.closed(tid, path,
                     kind == CAPIO_FS_CLOSE_EXPLICIT ? CloseKind::EXPLICIT : CloseKind::IMPLICIT,
                     writable != 0);
    }
}

void close_handler(const char *str) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    if (fd == -1) {
        handle_close_fs(tid, str);
        return;
    }
    handle_close(tid, fd);
}

#endif // CAPIO_SERVER_HANDLERS_CLOSE_HPP
