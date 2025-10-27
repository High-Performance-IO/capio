#ifndef CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#define CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#include "clone.hpp"

void handshake_anonymous_handler(const char *const str) {
    int tid, pid;
    sscanf(str, "%d %d", &tid, &pid);
    client_manager->registerClient(tid);
}

void handshake_named_handler(const char *const str) {
    int tid, pid, wait;
    char app_name[1024];
    sscanf(str, "%d %d %s %d", &tid, &pid, app_name, &wait);
    client_manager->registerClient(tid, app_name, static_cast<bool>(wait));
}

#endif // CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
