#ifndef CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#define CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#include "clone.hpp"
#include "utils/common.hpp"

void handshake_anonymous_handler(const char *const str) {
    int tid, pid;
    sscanf(str, "%d %d", &tid, &pid);
    client_manager->registerClient(tid, CAPIO_DEFAULT_APP_NAME, false, pid);
}

void handshake_named_handler(const char *const str) {
    int tid, pid, wait;
    char app_name[1024];
    char variant = CAPIO_VARIANT_MEMORY;
    sscanf(str, "%d %d %s %d %c", &tid, &pid, app_name, &wait, &variant);
    client_manager->registerClient(tid, app_name, static_cast<bool>(wait), pid, variant);
}

#endif // CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
