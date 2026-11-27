#ifndef CAPIO_SERVER_HANDLERS_UNLINK_HPP
#define CAPIO_SERVER_HANDLERS_UNLINK_HPP
#include "client-manager/client_manager.hpp"
#include "storage/manager.hpp"
#include "utils/common.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;

inline void unlink_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) != 1) {
        return;
    }
    const std::string path = request_text(tid, str, consumed);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    const auto c_file_opt = storage_manager->tryGet(path);
    if (c_file_opt) { // TODO: it works only in the local case
        CapioFile &c_file = c_file_opt->get();
        if (c_file.deletable()) {
            storage_manager->remove(path);
            storage_manager->removeFromDirectory(path);
            home_node_service->remove(path);
        }
        client_manager->replyToClient(tid, 0);
    } else {
        client_manager->replyToClient(tid, -1);
    }
}

#endif // CAPIO_SERVER_HANDLERS_UNLINK_HPP
