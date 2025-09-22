#ifndef MTCL_BACKEND_HPP
#define MTCL_BACKEND_HPP

#include <include/communication-service/data-plane/backend_interface.hpp>
#include <queue>
#include <thread>
#include <unordered_map>

/**
 * This avoids it to include the MTCL library here as it is a header-only library.
 * this is equivalent to use extern in C but for class
 */
namespace MTCL {
class HandleUser;
}

class MTCLBackend : public BackendInterface {

    std::string selfToken, connectedHostname, ownPort, usedProtocol;
    std::unordered_map<std::string, MessageQueue *> open_connections;
    char ownHostname[HOST_NAME_MAX] = {0};
    int thread_sleep_times          = 0;
    bool *continue_execution        = new bool;
    std::mutex *_guard;
    std::thread *th;
    std::vector<std::thread *> connection_threads;
    bool *terminate;

    /**
     * This thread handles a single p2p connection with another capio_server instance
     * @param HandlerPointer A MTCL valid HandlePointer
     * @param remote_hostname The remote endpoint of the connection handled by this thread
     * @param queue A pointer to a queue to communicate with other components. Queue has pointers to
     * inbound and outbound sub-queues for inbound and outbound messages
     * @param sleep_time How long to sleep between thread cycle
     * @param terminate A reference to a boolean heap allocated variable that is controlled by the
     * main thread that tells when to terminate the execution
     */
    void static serverConnectionHandler(MTCL::HandleUser HandlerPointer,
                                        const std::string &remote_hostname, MessageQueue *queue,
                                        int sleep_time, const bool *terminate);

    /**
     * Waits for incoming new requests to connect to new server instances. When a new request
     * arrives, it then handshakes with the remote servers, opening a new connection, and starting a
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
        std::unordered_map<std::string, MessageQueue *> *open_connections, std::mutex *guard,
        std::vector<std::thread *> *_connection_threads, bool *terminate);

    /**
     * Explode request into request code and request arguments
     * @param req Original request string as received from the remote node
     * @param args output string with parameters of the request
     * @return  request code
     */
    static int read_next_request(char *req, char *args);

  public:
    explicit MTCLBackend(const std::string &proto, const std::string &port, int sleep_time);

    ~MTCLBackend() override;

    void connect_to(std::string hostname_port) override;

    std::vector<std::string> get_open_connections() override;
    std::tuple<size_t, char *> fetchFromRemoteHost(const std::string &hostname,
                                                   const std::filesystem::path &filepath,
                                                   capio_off64_t offset,
                                                   capio_off64_t count) override;
};

#endif // MTCL_BACKEND_HPP
