#ifndef CAPIO_SERVER_HANDLERS_ACCESS_HPP
#define CAPIO_SERVER_HANDLERS_ACCESS_HPP
#include "client-manager/client_manager.hpp"
#include "client-manager/handlers.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/location.hpp"

extern ClientManager *client_manager;

void access_handler(const char *const str) {
    START_LOG(gettid(), "call(str=%s)", str);
    long tid;
    char path[PATH_MAX];
    sscanf(str, "%ld %s", &tid, path);

    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }

    client_manager->replyToClient(tid, get_file_location_opt(path) ? 0 : -1);
}

#endif // CAPIO_SERVER_HANDLERS_ACCESS_HPP
