#ifndef HANDSHAKE_HPP
#define HANDSHAKE_HPP

#include "capio/constants.hpp"

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
}

#endif // HANDSHAKE_HPP
