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
#include "remote/backend.hpp"

typedef unsigned long long int capio_off64_t;

template <typename T> class AtomicQueue {
    // data, sizeof(data), hostname
    std::queue<std::tuple<T, size_t, std::string>> _queue;
    std::mutex _mutex;
    std::condition_variable _lock_cond;

    bool _shutdown = false;

  public:
    ~AtomicQueue() {
        {
            std::lock_guard lg(_mutex);
            _shutdown = true;
        }
        _lock_cond.notify_all();
    }

    void push(T message, size_t message_size, const std::string &origin) {
        {
            std::lock_guard lg(_mutex);
            if (_shutdown) {
                return;
            }
            _queue.emplace(message, message_size, origin);
        }
        _lock_cond.notify_all();
    }

    std::tuple<T, size_t, std::string> pop() {
        std::unique_lock lock(_mutex);
        _lock_cond.wait(lock, [this] { return !_queue.empty() || _shutdown; });
        auto s = std::move(_queue.front());
        _queue.pop();

        return s;
    }

    std::optional<std::tuple<T, size_t, std::string>> try_pop() {
        std::lock_guard lg(_mutex);
        if (_queue.empty() || _shutdown) {
            return std::nullopt;
        }

        auto s = std::move(_queue.front());
        _queue.pop();
        return s;
    }
};

/**
 * This avoids it to include the MTCL library here as it is a header-only library.
 * this is equivalent to use extern in C but for class
 */
namespace MTCL {
class HandleUser;
}

// TODO: extend backend class
class MTCLBackend : public Backend {

    std::string selfToken, connectedHostname, ownPort, usedProtocol;
    std::unordered_map<std::string, AtomicQueue<const char *> *> open_connections;
    std::string ownHostname;
    int thread_sleep_times   = 0;
    bool *continue_execution = new bool;
    std::mutex *_guard;
    std::thread *incoming_MTCL_connection_listener_thread = nullptr;
    std::thread *incoming_UDP_connection_listener_thread  = nullptr;
    std::vector<std::thread *> connection_threads;
    bool *terminate;

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
     * @param guard
     * @param _connection_threads
     * @param terminate
     */
    void static incomingMTCLConnectionListener(
        const std::string &ownHostname, const std::string &ownPort, const std::string &usedProtocol,
        const bool *continue_execution, int sleep_time,
        std::unordered_map<std::string, AtomicQueue<const char *> *> *open_connections,
        std::mutex *guard, std::vector<std::thread *> *_connection_threads, bool *terminate,
        AtomicQueue<std::string> *incoming_request_queue);

    /**
     * Initiate a new MTCL connection with "out of band" communication through multicast
     * advertisement. when a multicast advertisement is received, start the MTCL handshake with the
     * remote server instance.
     */
    static void incomingUDPConnectionListener(
        bool *terminate, const std::string &ownHostname, std::string ownPort,
        std::string usedProtocol,
        std::unordered_map<std::string, AtomicQueue<const char *> *> *open_connections,
        std::vector<std::thread *> *connection_threads, int thread_sleep_time,
        AtomicQueue<std::string> *incoming_request_queue, std::mutex *_guard);

  public:
    explicit MTCLBackend(const std::string &proto, const std::string &port, int sleep_time);

    ~MTCLBackend() override;

    RemoteRequest read_next_request() override;

    void handshake_servers() override;

    const std::set<std::string> get_nodes() override;

    void send_request(const char *message, int message_len, const std::string &target) override;

    void send_file(char *shm, long int nbytes, const std::string &target) override;

    void recv_file(char *shm, const std::string &source, long int bytes_expected) override;
};

#endif // MTCL_BACKEND_HPP