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

    client_manager->register_client(app_name, tid);
    storage_service->register_client(app_name, tid);
    /*
     * The handshake request must be blocking ONLY when not building tests. This is because when
     * starting unit tests, the binary is loaded with libcapio_posix.so underneath thus performing
     * a handshake request. If the handshake is blocking, then the capio_server binary cannot be
     * started as the whole process is waiting for a handshake.
     */
#ifndef CAPIO_BUILD_TESTS
    // If not on termination phase, return 1. Otherwise, return 0
    // if - is returned posix application will terminate
    if (!termination_phase) {
        // Unlock client waiting to start
        LOG("Allowing handshake to continue");
        client_manager->reply_to_client(tid, 1);
    } else {
        LOG("Termination phase is in progress. ignoring further handshakes.");
        client_manager->reply_to_client(tid, 0);
    }
#endif
}

#endif // HANDSHAKE_HPP
