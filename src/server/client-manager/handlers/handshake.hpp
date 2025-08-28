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

    if (!capio_global_configuration->termination_phase) {
        client_manager->register_client(app_name, tid);
        storage_service->register_client(app_name, tid);
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                       "Registered new app: " + std::string(app_name));

        // Unlock client waiting to start
        LOG("Allowing handshake to continue");
        client_manager->reply_to_client(tid, 1);

    } else {
        LOG("Termination phase is in progress. ignoring further handshakes.");
        client_manager->reply_to_client(tid, 0);
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       "Termination phase is in progress. "
                       "ignoring further handshakes.");

    }
}

#endif // HANDSHAKE_HPP