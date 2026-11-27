#ifndef CAPIO_SERVER_HANDLERS_ACCESS_HPP
#define CAPIO_SERVER_HANDLERS_ACCESS_HPP

#include "remote/home_node_service.hpp"
#include "utils/common.hpp"

extern ClientManager *client_manager;

inline void access_handler(const char *const str) {
    START_LOG(gettid(), "call(str=%s)", str);
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) != 1) {
        return;
    }
    const std::string path = request_text(tid, str, consumed);

    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }

    client_manager->replyToClient(tid, home_node_service->getHome(path) ? 0 : -1);
}

#endif // CAPIO_SERVER_HANDLERS_ACCESS_HPP
