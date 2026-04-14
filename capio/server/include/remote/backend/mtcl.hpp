#ifndef MTCL_BACKEND_HPP
#define MTCL_BACKEND_HPP

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/constants.hpp"
#include "common/logger.hpp"
#include "remote/atomic_queue.hpp"
#include "remote/backend.hpp"

#include <shared_mutex>

typedef unsigned long long int capio_off64_t;

/**
 * This avoids it to include the MTCL library here as it is a header-only library.
 * this is equivalent to use extern in C but for class
 */
namespace MTCL {
class HandleUser;
}

// TODO: extend backend class
class MTCLBackend : public Backend {

    int thread_sleep_times  = 0;
    bool continue_execution = true;

    const std::string selfToken, ownPort, usedProtocol;

    std::shared_mutex open_connections_lock;
    std::unordered_map<std::string, AtomicQueue<const char *> *> open_connections;

    std::thread *incoming_connection_thread = nullptr;
    std::vector<std::thread *> connection_threads;

    AtomicQueue<std::string> incoming_request_queue;

    /**
     * Waits for incoming new requests to connect to new server instances. When a new request
     * arrives, it then handshakes with the remote servers, opening a new connection, and starting a
     * new thread that will handle remote requests. If no request arrives within the sleep_time
     * parameter, then the method will issue an advertisement on UDP multicast of its alive state
     * so that other servers may instantiate a new connection with me.
     *
     * @param ownHostname
     * @param ownPort
     * @param usedProtocol
     * @param continue_execution
     * @param sleep_time
     * @param open_connections
     * @param open_connection_guard
     * @param _connection_threads
     * @param incoming_request_queue
     */
    void static incomingMTCLConnectionListener(
        const std::string &ownHostname, const std::string &ownPort, const std::string &usedProtocol,
        const bool *continue_execution, int sleep_time,
        std::unordered_map<std::string, AtomicQueue<const char *> *> *open_connections,
        std::shared_mutex *open_connection_guard, std::vector<std::thread *> *_connection_threads,
        AtomicQueue<std::string> *incoming_request_queue);

  public:
    explicit MTCLBackend(const std::string &proto, const std::string &port, int sleep_time);

    ~MTCLBackend() override;

    RemoteRequest read_next_request() override;

    void handshake_servers() override;

    const std::set<std::string> get_nodes() override;

    void send_request(const char *message, int message_len, const std::string &target) override;

    void send_file(char *shm, long int nbytes, const std::string &target) override;

    void recv_file(char *shm, const std::string &source, long int bytes_expected) override;

    void connect_to(const std::string &target_token) override;
};

#endif // MTCL_BACKEND_HPP