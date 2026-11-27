#ifndef CAPIO_SERVER_HANDLERS_MKDIR_HPP
#define CAPIO_SERVER_HANDLERS_MKDIR_HPP

#include "client-manager/client_manager.hpp"
#include "storage/manager.hpp"
#include "utils/common.hpp"

extern ClientManager *client_manager;
extern StorageManager *storage_manager;

inline void mkdir_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) != 1) {
        return;
    }
    const std::string pathname = request_text(tid, str, consumed);
    client_manager->replyToClient(tid, storage_manager->addDirectory(tid, pathname));
}

#endif // CAPIO_SERVER_HANDLERS_MKDIR_HPP
