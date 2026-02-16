#ifndef CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#define CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#include <cstdio>

#include "client/manager.hpp"
#include "client/request.hpp"

extern ClientManager *client_manager;
void ClientRequestManager::ClientHandlers::handshake_anonymous_handler(const char *const str) {
    int tid, pid;
    sscanf(str, "%d %d", &tid, &pid);
    client_manager->registerClient(tid);
}

void ClientRequestManager::ClientHandlers::handshake_named_handler(const char *const str) {
    int tid, pid, wait;
    char app_name[1024];
    sscanf(str, "%d %d %s %d", &tid, &pid, app_name, &wait);
    client_manager->registerClient(tid, app_name, static_cast<bool>(wait));
}

#endif // CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
