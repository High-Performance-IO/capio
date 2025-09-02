#ifndef MTCL_BACKEND_HPP
#define MTCL_BACKEND_HPP

#include <include/communication-service/data-plane/backend_interface.hpp>
#include <queue>
#include <thread>
#include <unordered_map>

/**
 * This avoids to include the MTCL library here as it is a header only library.
 * this is equivalent to use extern in C but for class
 */
namespace MTCL {
class HandleUser;
}

class MTCLBackend : public BackendInterface {
    typedef enum { FROM_REMOTE, TO_REMOTE } CONN_HANDLER_ORIGIN;

    std::string selfToken, connectedHostname, ownPort, usedProtocol;
    std::unordered_map<std::string, std::queue<std::string>> open_connections;
    char ownHostname[HOST_NAME_MAX] = {0};
    int thread_sleep_times          = 0;
    bool *continue_execution        = new bool;
    std::mutex *_guard;
    std::thread *th;
    std::vector<std::thread *> connection_threads;
    bool *terminate;

    /**
     * This thread handles a single p2p connection with another capio_server instance
     * @param HandlerPointer
     * @param remote_hostname
     * @param outbound_messages
     * @param sleep_time
     * @param terminate
     * @param source
     */
    void static serverConnectionHandler(MTCL::HandleUser HandlerPointer,
                                        const std::string &remote_hostname,
                                        std::queue<std::string> *outbound_messages, int sleep_time,
                                        const bool *terminate, CONN_HANDLER_ORIGIN source);

    /**
     * Waits for incoming new requests to connect to new server instances. When a new request
     * arrives it then handshakes with the remote servers, opening a new connection, and starting a
     * new thread that will handle remote requests
     *
     * @param continue_execution
     * @param sleep_time
     * @param open_connections
     * @param guard
     * @param _connection_threads
     * @param terminate
     */
    void static incomingConnectionListener(
        const bool *continue_execution, int sleep_time,

        std::unordered_map<std::string, std::queue<std::string>> *open_connections,
        std::mutex *guard, std::vector<std::thread *> *_connection_threads, bool *terminate);

  public:
    explicit MTCLBackend(const std::string &proto, const std::string &port, int sleep_time);

    ~MTCLBackend() override;

    void connect_to(std::string hostname_port) override;

    std::vector<std::string> get_open_connections() override;
    size_t fetchFromRemoteHost(const std::string &hostname, const std::filesystem::path &filepath,
                               char *buffer, capio_off64_t offset, capio_off64_t count) override;
};

#endif // MTCL_BACKEND_HPP
