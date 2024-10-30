#ifndef HANDSHAKE_HPP
#define HANDSHAKE_HPP

#include "capio/constants.hpp"

#include <storage-service/capio_storage_service.hpp>

/**
 * @brief Perform handshake without the application name being provided.
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
inline void handshake_anonymous_handler(const char *const str) {
    pid_t tid, pid;
    sscanf(str, "%d %d", &tid, &pid);
    START_LOG(gettid(), "call(tid=%ld, pid=%ld)", tid, pid);
    client_manager->register_new_client(tid, CAPIO_DEFAULT_APP_NAME);
    const capio_off64_t count = storage_service->register_client(CAPIO_DEFAULT_APP_NAME);
    client_manager->reply_to_client(tid, count);
}

/**
 * @brief Perform handshake while providing the posix application name
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
inline void handshake_named_handler(const char *const str) {
    pid_t tid, pid;
    char app_name[1024];
    sscanf(str, "%d %d %s", &tid, &pid, app_name);
    START_LOG(gettid(), "call(tid=%ld, pid=%ld, app_name=%s)", tid, pid, app_name);
    client_manager->register_new_client(tid, app_name);
    const capio_off64_t count = storage_service->register_client(app_name);
    client_manager->reply_to_client(tid, count);
}

#endif // HANDSHAKE_HPP
