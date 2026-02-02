#ifndef CAPIO_SERVER_HANDLERS_MKDIR_HPP
#define CAPIO_SERVER_HANDLERS_MKDIR_HPP

#include "client-manager/client_manager.hpp"
#include "storage/manager.hpp"

extern ClientManager *client_manager;
extern StorageManager *storage_manager;

void mkdir_handler(const char *const str) {
    pid_t tid;
    char pathname[PATH_MAX];
    sscanf(str, "%d %s", &tid, pathname);
    client_manager->replyToClient(tid, storage_manager->addDirectory(tid, pathname));
}

#endif // CAPIO_SERVER_HANDLERS_MKDIR_HPP
