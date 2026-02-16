#ifndef CAPIO_SERVER_HANDLERS_MKDIR_HPP
#define CAPIO_SERVER_HANDLERS_MKDIR_HPP

#include "client/manager.hpp"
#include "client/request.hpp"
#include "storage/manager.hpp"

extern ClientManager *client_manager;
extern StorageManager *storage_manager;

void ClientRequestManager::ClientHandlers::mkdir_handler(const char *const str) {
    pid_t tid;
    char pathname[PATH_MAX];
    sscanf(str, "%d %s", &tid, pathname);
    client_manager->replyToClient(tid, storage_manager->addDirectory(tid, pathname));
}

#endif // CAPIO_SERVER_HANDLERS_MKDIR_HPP
