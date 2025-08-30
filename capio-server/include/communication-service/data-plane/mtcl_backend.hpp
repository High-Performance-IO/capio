#ifndef MTCL_BACKEND_HPP
#define MTCL_BACKEND_HPP

#include <include/communication-service/data-plane/backend_interface.hpp>
#include <include/communication-service/data-plane/transport_unit.hpp>
#include <queue>
#include <thread>

/**
 * This avoid to include the MTCL librari here as it is a header only library.
 * this is equivalent to use extern in C but for class
 */
namespace MTCL {
class HandleUser;
}

class MTCLBackend : public BackendInterface {
    typedef enum { FROM_REMOTE, TO_REMOTE } CONN_HANDLER_ORIGIN;

    typedef std::tuple<std::queue<TransportUnit *> *, std::queue<TransportUnit *> *, std::mutex *>
        TransportUnitInterface;
    std::unordered_map<std::string, TransportUnitInterface> connected_hostnames_map;
    std::string selfToken, connectedHostname, ownPort, usedProtocol;
    char ownHostname[HOST_NAME_MAX] = {0};
    int thread_sleep_times          = 0;
    bool *continue_execution        = new bool;
    std::mutex *_guard;
    std::thread *th;
    std::vector<std::thread *> connection_threads;
    bool *terminate;

    static TransportUnit *receive_unit(MTCL::HandleUser *HandlerPointer);

    static void send_unit(MTCL::HandleUser *HandlerPointer, const TransportUnit *unit);

    /**
     * This thread will handle connections towards a single target.
     */
    void static server_connection_handler(MTCL::HandleUser HandlerPointer,
                                          const std::string remote_hostname, const int sleep_time,
                                          TransportUnitInterface interface, const bool *terminate,
                                          CONN_HANDLER_ORIGIN source);

    void static incoming_connection_listener(
        const bool *continue_execution, int sleep_time,
        std::unordered_map<std::string, TransportUnitInterface> *open_connections,
        std::mutex *guard, std::vector<std::thread *> *_connection_threads, bool *terminate);

  public:
    void connect_to(std::string hostname_port) override;

    explicit MTCLBackend(const std::string &proto, const std::string &port, int sleep_time);

    ~MTCLBackend() override;

    std::string receive(char *buf, capio_off64_t *buf_size, capio_off64_t *start_offset) override;

    void send(const std::string &target, char *buf, uint64_t buf_size, const std::string &filepath,
              const capio_off64_t start_offset) override;

    std::vector<std::string> get_open_connections() override;
};

#endif // MTCL_BACKEND_HPP
