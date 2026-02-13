#ifndef CAPIO_SERVER_HANDLERS_UNLINK_HPP
#define CAPIO_SERVER_HANDLERS_UNLINK_HPP
#include "client-manager/client_manager.hpp"
#include "storage/manager.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;

void unlink_handler(const char *const str) {
    char path[PATH_MAX];
    int tid;
    sscanf(str, "%d %s", &tid, path);
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path);
    if (CapioCLEngine::get().isExcluded(path)) {
        LOG("File is excluded. Skipping removal");
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    const auto c_file_opt = storage_manager->tryGet(path);
    if (c_file_opt) { // TODO: it works only in the local case
        LOG("Found local path");
        CapioFile &c_file = c_file_opt->get();
        if (c_file.is_deletable()) {
            LOG("File is deletable!");
            storage_manager->remove(path);
            delete_from_files_location(path);
        }
        client_manager->replyToClient(tid, 0);
    } else {
        LOG("File not found");
        client_manager->replyToClient(tid, -1);
    }
}

#endif // CAPIO_SERVER_HANDLERS_UNLINK_HPP
