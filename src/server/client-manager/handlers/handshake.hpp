#ifndef HANDSHAKE_HPP
#define HANDSHAKE_HPP

#include "capio/constants.hpp"

#include <storage-service/capio_storage_service.hpp>

/**
 * @brief Perform handshake while providing the posix application name
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
inline void handshake_handler(const char *const str) {
    pid_t tid, pid;
    char app_name[1024];
    sscanf(str, "%d %d %s", &tid, &pid, app_name);
    START_LOG(gettid(), "call(tid=%ld, pid=%ld, app_name=%s)", tid, pid, app_name);
    if (termination_phase) {
        LOG("Termination phase is in progress. ignoring further handshakes.");
        return;
    }
    client_manager->register_client(app_name, tid);
    storage_service->register_client(app_name, tid);
    // Unlock client waiting to start
    client_manager->reply_to_client(tid, 1);
}

#endif // HANDSHAKE_HPP
