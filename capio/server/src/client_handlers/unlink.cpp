#ifndef CAPIO_SERVER_HANDLERS_UNLINK_HPP
#define CAPIO_SERVER_HANDLERS_UNLINK_HPP
#include "client/manager.hpp"
#include "client/request.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/location.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;

void ClientRequestManager::MemHandlers::unlink_handler(const char *const str) {
    char path[PATH_MAX];
    int tid;
    sscanf(str, "%d %s", &tid, path);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    const auto c_file_opt = storage_manager->tryGet(path);
    if (c_file_opt) { // TODO: it works only in the local case
        CapioFile &c_file = c_file_opt->get();
        if (c_file.is_deletable()) {
            storage_manager->remove(path);
            delete_from_files_location(path);
        }
        client_manager->replyToClient(tid, 0);
    } else {
        client_manager->replyToClient(tid, -1);
    }
}

#endif // CAPIO_SERVER_HANDLERS_UNLINK_HPP
