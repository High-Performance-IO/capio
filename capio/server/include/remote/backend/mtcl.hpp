#ifndef MTCL_BACKEND_HPP
#define MTCL_BACKEND_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/constants.hpp"
#include "common/logger.hpp"
#include "remote/backend.hpp"

typedef unsigned long long int capio_off64_t;

namespace MTCL {
class HandleUser;
}

/** Per-peer MTCL handle and asynchronous send state, defined in the implementation file. */
struct MTCLConnection;

/**
 * CAPIO remote backend using MTCL for dynamic point-to-point communication.
 *
 * Incoming connections and messages are multiplexed through MTCL::Manager::getNext(). Each peer
 * retains a yielded handle for asynchronous sends, avoiding a CAPIO worker thread per connection.
 */
class MTCLBackend : public Backend {

    /// Timeout used while waiting for the next MTCL event.
    int thread_sleep_times  = 0;
    /// Controls the receive-dispatch loop during shutdown.
    std::atomic_bool continue_execution{true};

    /// MTCL listener, discovery advertisement, port, and protocol identifiers.
    const std::string listen_token, advertisement_token, ownPort, usedProtocol;

    /// Connected peers indexed by hostname and protected against discovery/send races.
    std::shared_mutex open_connections_lock;
    std::unordered_map<std::string, std::unique_ptr<MTCLConnection>> open_connections;

    /** File payload retained between decoding a compound READ_REPLY and handling that request. */
    struct PendingFile {
        /// Hostname from which the frame was received.
        std::string source;
        /// Complete transaction frame owning the file bytes.
        std::vector<unsigned char> frame;
        /// Offset of the file payload within @ref frame.
        size_t offset;
    };
    std::optional<PendingFile> pending_file;

    /** Complete the CAPIO hostname handshake and register a newly accepted handle. */
    void accept_connection(MTCL::HandleUser handle);

    /** Queue one owned request/file transaction for asynchronous transmission. */
    void send_transaction(const char *message, size_t message_len, const char *file,
                          size_t file_len, const std::string &target);

    /** Release completed sends and remove connections whose sends failed. */
    void cleanup_completed_sends();

    /** Remove a peer so discovery can establish a replacement connection. */
    void remove_connection(const std::string &hostname);

  public:
    explicit MTCLBackend(const std::string &proto, const std::string &port, int sleep_time);

    ~MTCLBackend() override;

    RemoteRequest read_next_request() override;

    void handshake_servers() override;

    const std::set<std::string> get_nodes() override;

    void send_request(const char *message, int message_len, const std::string &target) override;

    void send_file(char *shm, long int nbytes, const std::string &target) override;

    void send_request_with_file(const char *message, int message_len, char *shm, long int nbytes,
                                const std::string &target) override;

    void recv_file(char *shm, const std::string &source, long int bytes_expected) override;

    void connect_to(const std::string &target_token) override;
};

#endif // MTCL_BACKEND_HPP
