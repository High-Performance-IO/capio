#ifndef CAPIO_SERVER_HANDLERS_CLOSE_HPP
#define CAPIO_SERVER_HANDLERS_CLOSE_HPP

#include "common/requests.hpp"
#include "read.hpp"
#include "utils/common.hpp"

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

/**
 * @brief "tid fd [error]": error is what the close failed with, 0 if it worked (FS variant only).
 * A descriptor of the FS variant is known by its process, from the CAPIO_REQUEST_CREATE_FS that
 * only that variant sends: any other one is of the memory variant.
 */
void close_handler(const char *str) {
    int tid, fd, error = 0;
    sscanf(str, "%d %d %d", &tid, &fd, &error);
    if (const pid_t pid = client_manager->getPid(tid);
        !fs_storage_manager->getPath(pid, fd).empty()) {
        fs_storage_manager->closed(tid, pid, fd, error); // a close that failed lost data
    } else {
        handle_close(tid, fd);
    }
}

#endif // CAPIO_SERVER_HANDLERS_CLOSE_HPP
