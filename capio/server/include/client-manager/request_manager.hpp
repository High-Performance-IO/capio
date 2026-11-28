#ifndef CAPIO_SERVER_CLIENT_MANAGER_REQUEST_MANAGER_HPP
#define CAPIO_SERVER_CLIENT_MANAGER_REQUEST_MANAGER_HPP

#include <chrono>

#include "utils/types.hpp"

/**
 * @brief Serves the requests that clients send to this server, one at a time, each with the
 * handler of its code (CAPIO_REQUEST_* in common/requests.hpp).
 */
class ClientRequestManager {
  public:
    /// @return The handler of the client request @p code, nullptr if it is not one
    [[nodiscard]] static CSHandler_t handlerOf(int code);

    /**
     * @brief Handle the client request @p code, whose text (after the code) is @p request
     * @return false, having done nothing, if @p code is not a client request
     */
    static bool handle(int code, const char *request);

    /**
     * @brief Let the request being handled, if any, finish, and handle no other: for the teardown
     * of the server, before it frees the managers the handlers use. It waits at most @p timeout,
     * and not at all when called by a handler (a fault in it).
     * @return Whether no request is being handled any more
     */
    static bool stop(std::chrono::milliseconds timeout);

    /// @brief Serve the requests of the clients, as they arrive, until the server exits. A code
    /// that is not a request ends the server: the client and the server do not agree on the
    /// protocol.
    [[noreturn]] static void start();
};

#endif // CAPIO_SERVER_CLIENT_MANAGER_REQUEST_MANAGER_HPP
