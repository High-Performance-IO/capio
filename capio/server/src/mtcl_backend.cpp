#include "remote/backend/mtcl.hpp"
#include "remote/discovery.hpp"
#include "utils/common.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mtcl.hpp>
#include <stdexcept>

extern DiscoveryService *discovery_service;

/**
 * MTCL message layout:
 *
 * +----------+---------------------+------------------+-----------------+----------------+
 * | type     | request size (BE)   | file size (BE)   | request bytes   | file bytes     |
 * +----------+---------------------+------------------+-----------------+----------------+
 * | 1 byte   | 8 bytes             | 8 bytes          | request size    | file size      |
 * +----------+---------------------+------------------+-----------------+----------------+
 * \_________________ 17-byte header _________________/   optional when file size is zero
 *
 * NOTE: constants are defined within this source file to avoid leakage across CAPIO
 *
 */

/**Header size of MTCL message**/
constexpr size_t MTCL_HEADER_SIZE = 17;

/** Maximum file payload accepted by one MTCL message. Set to 4GB*/
constexpr std::uint64_t MTCL_MAX_FILE_TRANSFER_SIZE = 4ULL * 1024 * 1024 * 1024;

/** Maximum size a MTCL sent frame can have **/
constexpr size_t MTCL_MAX_FRAME_SIZE = MTCL_HEADER_SIZE + CAPIO_SERVER_REQUEST_MAX_SIZE +
                                       static_cast<size_t>(MTCL_MAX_FILE_TRANSFER_SIZE);

/** Identifies whether an MTCL message includes a file payload. */
typedef enum class MessageType : unsigned char { request = 1, request_with_file = 2 } MessageType;

/**
 * @brief Owns an MTCL connection and the buffers used by asynchronous sends.
 *
 * Sent frames remain owned by the connection until MTCL reports their completion.
 */
class MTCLConnection {

    struct PendingSend {
        std::vector<unsigned char> frame;
        MTCL::Request request;
    };

    MTCL::HandleUser handle;
    std::mutex send_lock;
    std::vector<PendingSend> pending_sends;

    /**
     * @brief Removes finished sends and detects short writes while the caller holds send_lock.
     * @return false when MTCL completed a send without transferring its entire frame.
     *
     * The unlocked form lets send() and cleanup_completed_sends() share the completion loop
     * without locking the same non-recursive mutex twice.
     */
    bool _cleanup_completed_sends() {
        for (auto send = pending_sends.begin(); send != pending_sends.end();) {
            if (!MTCL::test(send->request)) {
                ++send;
            } else if (send->request.count() != static_cast<ssize_t>(send->frame.size())) {
                return false;
            } else {
                send = pending_sends.erase(send);
            }
        }
        return true;
    }

  public:
    /**
     * @brief Takes ownership of an established MTCL handle.
     * @param connection_handle Handle connected to a remote CAPIO server.
     */
    explicit MTCLConnection(MTCL::HandleUser connection_handle)
        : handle(std::move(connection_handle)) {}

    /** @brief Cancels pending sends and closes the connection. */
    ~MTCLConnection() {
        pending_sends.clear();
        handle.close();
    }

    /** The connection is the unique owner of its MTCL handle and pending sends. */
    MTCLConnection(const MTCLConnection &)            = delete;
    /** The connection cannot be copied because its handle, mutex, and sends have unique ownership.
     */
    MTCLConnection &operator=(const MTCLConnection &) = delete;

    /** @brief Returns receive-side ownership of the handle to MTCL. */
    void yield() { handle.yield(); }

    /**
     * @brief Starts an asynchronous send of a complete message frame.
     * @param message Frame retained until the send completes.
     * @return true if MTCL accepted the send, false if the connection failed.
     */
    bool send(std::vector<unsigned char> message) {
        const std::lock_guard lock(send_lock);
        if (!_cleanup_completed_sends()) {
            return false;
        }

        // NOTE: MTCL requires the message buffer and request to remain alive until the send
        // completes. also, the isend is async so a reference needs to keep existing
        pending_sends.push_back({std::move(message), {}});
        auto &[frame, request] = pending_sends.back();
        if (handle.isend(frame.data(), frame.size(), request) < 0) {
            pending_sends.pop_back();
            return false;
        }
        return true;
    }

    /**
     * @brief Releases buffers for completed sends.
     * @return false if a completed send transferred an unexpected number of bytes.
     *
     * Periodic cleanup is required because MTCL sends are asynchronous and their buffers cannot
     * be released when send() returns.
     */
    bool cleanup_completed_sends() {
        const std::lock_guard lock(send_lock);
        return _cleanup_completed_sends();
    }
};

/**
 * @brief Encodes a 64-bit integer in big-endian order.
 *
 * A fixed byte order makes frame sizes independent of the sender's architecture.
 */
static void write_u64(unsigned char *destination, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        destination[i] = static_cast<unsigned char>(value);
        value >>= 8;
    }
}

/** @brief Decodes a big-endian 64-bit frame field written by write_u64(). */
static uint64_t read_u64(const unsigned char *source) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | source[i];
    }
    return value;
}

/**
 * @brief Receives one MTCL message and verifies that it exactly fills the expected buffer.
 * @return true only when all expected bytes were received.
 */
static bool receive_message(MTCL::HandleUser &handle, void *data, size_t size) {
    return handle.receive(data, size) == static_cast<ssize_t>(size);
}

/**
 * @brief Drains a malformed MTCL message before its connection is removed.
 *
 * Consuming the queued message prevents MTCL from repeatedly reporting the same invalid input.
 */
static void discard_message(MTCL::HandleUser &handle) {
    char byte = 0;
    if (MTCL::Request request; handle.ireceive(&byte, sizeof(byte), request) == 0) {
        [[maybe_unused]] const auto r = request.wait();
    }
}

/**
 * @brief Waits for, validates, and decodes the next request from any MTCL peer.
 * @return The decoded request and its source, or empty strings during shutdown.
 *
 * MTCL multiplexes new connections and readable handles through getNext(), so this method also
 * accepts peers, removes failed connections, and retains an attached file for recv_file().
 */
RemoteRequest MTCLBackend::read_next_request() {
    START_LOG(gettid(), "call()");
    // A valid READ_REPLY consumes this before asking for another request.
    pending_file.reset();
    while (continue_execution) {
        cleanup_completed_sends();
        auto handle = MTCL::Manager::getNext(std::chrono::microseconds(thread_sleep_times));
        if (!handle.isValid()) {
            continue;
        }
        if (handle.isNewConnection()) {
            accept_connection(std::move(handle));
            continue;
        }

        const std::string remote_hostname = handle.getName();
        size_t available                  = 0;
        if (handle.probe(available) <= 0) {
            remove_connection(remote_hostname);
            continue;
        }

        if (available < MTCL_HEADER_SIZE || available > MTCL_MAX_FRAME_SIZE) {
            // fatal error of message out of admissible size
            discard_message(handle);
            remove_connection(remote_hostname);
            continue;
        }

        std::vector<unsigned char> frame(available);
        if (!receive_message(handle, frame.data(), frame.size())) {
            remove_connection(remote_hostname);
            continue;
        }

        const auto type             = static_cast<MessageType>(frame[0]);
        const uint64_t request_size = read_u64(frame.data() + 1);
        const uint64_t file_size    = read_u64(frame.data() + 9); // current op. file size
        if ((type != MessageType::request &&
             type != MessageType::request_with_file) ||         // not a request
            request_size == 0 ||                                // empty request
            request_size > CAPIO_SERVER_REQUEST_MAX_SIZE ||     // req. too big
            file_size > MTCL_MAX_FILE_TRANSFER_SIZE ||          // file size too big
            (type == MessageType::request && file_size != 0) || // request on file of 0 bytes
            request_size + file_size != available - MTCL_HEADER_SIZE) { // out of bounds
            remove_connection(remote_hostname);
            continue;
        }

        const auto request_begin = frame.begin() + MTCL_HEADER_SIZE;
        const auto file_begin    = request_begin + request_size;
        std::string request(request_begin, file_begin);

        if (type == MessageType::request_with_file) {
            if (pending_file) {
                remove_connection(remote_hostname);
                continue;
            }

            pending_file.emplace(PendingFile{remote_hostname, std::move(frame),
                                             MTCL_HEADER_SIZE + static_cast<size_t>(request_size)});
        }

        handle.yield();
        return {std::move(request), remote_hostname};
    }
    return {std::string{}, std::string{}};
}

/**
 * @brief Reads an incoming peer's hostname and registers its connection.
 *
 * CAPIO needs the hostname handshake because MTCL handles do not initially carry the node name
 * used as the open_connections key.
 */
void MTCLBackend::accept_connection(MTCL::HandleUser handle) {
    START_LOG(gettid(), "call()");
    size_t hostname_size = 0;
    if (!receive_message(handle, &hostname_size, sizeof(hostname_size)) || hostname_size == 0 ||
        hostname_size > HOST_NAME_MAX) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                         "Rejecting incoming MTCL connection: invalid hostname size %zu",
                         hostname_size);
        LOG("Rejecting incoming MTCL connection: invalid hostname size %zu", hostname_size);
        handle.close();
        return;
    }

    std::string remote_hostname(hostname_size, '\0');
    if (!receive_message(handle, remote_hostname.data(), remote_hostname.size())) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                         "Rejecting incoming MTCL connection: failed to receive hostname");
        LOG("Rejecting incoming MTCL connection: failed to receive hostname");
        handle.close();
        return;
    }

    handle.setName(remote_hostname);
    const std::unique_lock lock(open_connections_lock);
    if (open_connections.count(remote_hostname) != 0) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Rejecting duplicate MTCL connection from %s",
                         remote_hostname.c_str());
        LOG("Rejecting duplicate MTCL connection from %s", remote_hostname.c_str());
        handle.close();
        return;
    }
    auto connection = std::make_unique<MTCLConnection>(std::move(handle));
    connection->yield();
    open_connections.emplace(remote_hostname, std::move(connection));
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Connected to %s:%s:%s (incoming)", used_protocol.c_str(),
                     remote_hostname.c_str(), own_port.c_str());
    LOG("Connected to %s:%s:%s (incoming)", used_protocol.c_str(), remote_hostname.c_str(),
        own_port.c_str());
}

/**
 * @brief Encodes a request and optional file into one MTCL message and sends it to a peer.
 *
 * Keeping both payloads in one frame preserves their association; MTCLConnection retains that
 * frame until the asynchronous send completes.
 */
void MTCLBackend::send_frame(const char *message, size_t message_len, const char *file,
                             size_t file_len, const std::string &target) {
    std::vector<unsigned char> frame(MTCL_HEADER_SIZE + message_len + file_len);
    frame[0] = static_cast<unsigned char>(file == nullptr ? MessageType::request
                                                          : MessageType::request_with_file);
    write_u64(frame.data() + 1, message_len);
    write_u64(frame.data() + 9, file_len);
    std::memcpy(frame.data() + MTCL_HEADER_SIZE, message, message_len);
    if (file_len != 0) {
        std::memcpy(frame.data() + MTCL_HEADER_SIZE + message_len, file, file_len);
    }

    bool failed = false;
    {
        const std::shared_lock connections_lock(open_connections_lock);
        const auto found = open_connections.find(target);
        if (found == open_connections.end()) {
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "MTCL connection to %s is not available",
                             target.c_str());
            return;
        }
        auto &connection = *found->second;
        failed           = !connection.send(std::move(frame));
    }
    if (failed) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "MTCL connection to %s failed", target.c_str());
        remove_connection(target);
    }
}

/**
 * @brief Reclaims completed send buffers and removes peers whose sends failed.
 *
 * Failed hostnames are collected first because removing them while holding a shared map lock
 * would require an unsafe lock upgrade.
 */
void MTCLBackend::cleanup_completed_sends() {
    std::vector<std::string> failed;
    {
        const std::shared_lock connections_lock(open_connections_lock);
        for (auto &[hostname, connection] : open_connections) {
            if (!connection->cleanup_completed_sends()) {
                failed.emplace_back(hostname);
            }
        }
    }
    for (const auto &hostname : failed) {
        remove_connection(hostname);
    }
}

/**
 * @brief Removes a failed peer from the connection map.
 *
 * Erasing the entry closes its MTCLConnection and allows discovery to establish a replacement.
 */
void MTCLBackend::remove_connection(const std::string &hostname) {
    const std::unique_lock lock(open_connections_lock);
    const auto connection = open_connections.find(hostname);
    if (connection == open_connections.end()) {
        return;
    }
    open_connections.erase(connection);
}

/**
 * @brief Initializes MTCL and starts listening on the requested protocol and port.
 *
 * The advertisement token is retained so discovery can announce the same reachable endpoint.
 */
MTCLBackend::MTCLBackend(const std::string &proto, const std::string &port, const int sleep_time)
    : Backend(HOST_NAME_MAX), thread_sleep_times(sleep_time),
      listen_token(proto + ":0.0.0.0:" + port),
      advertisement_token(proto + ":" + node_name + ":" + port), own_port(port),
      used_protocol(proto) {
    MTCL::Manager::init("server-" + node_name);
    MTCL::Manager::listen(listen_token);
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "MTCL backend listening on %s", listen_token.c_str());
}

/**
 * @brief Stops request processing, closes all connections, and finalizes MTCL.
 *
 * Connections must be destroyed before Manager::finalize() invalidates MTCL resources.
 */
MTCLBackend::~MTCLBackend() {
    continue_execution = false;
    {
        const std::unique_lock lock(open_connections_lock);
        open_connections.clear();
    }
    MTCL::Manager::finalize();
}

/**
 * @brief Starts discovery advertising and listening for other CAPIO servers.
 *
 * Discovery supplies endpoint tokens to connect_to(); MTCL itself does not discover peers.
 */
void MTCLBackend::handshake_servers() {
    discovery_service->start(advertisement_token, std::max(1, thread_sleep_times / 1000));
}

/** @brief Returns a locked snapshot containing this node and every connected peer. */
const std::set<std::string> MTCLBackend::get_nodes() {
    std::set nodes{node_name};
    const std::shared_lock lock(open_connections_lock);
    for (const auto &[hostname, connection] : open_connections) {
        nodes.insert(hostname);
    }
    return nodes;
}

/**
 * @brief Validates and sends a request without a file payload.
 *
 * Validation protects frame size calculations and rejects invalid caller-owned buffers.
 */
void MTCLBackend::send_request(const char *message, const int message_len,
                               const std::string &target) {
    if (message == nullptr || message_len <= 0 ||
        static_cast<size_t>(message_len) > CAPIO_SERVER_REQUEST_MAX_SIZE) {
        throw std::invalid_argument("Invalid MTCL request");
    }
    send_frame(message, static_cast<size_t>(message_len), nullptr, 0, target);
}

/**
 * @brief Rejects standalone file sends because MTCL frames bind files to their requests.
 *
 * Callers must use send_request_with_file() so the receiver cannot associate a file with the
 * wrong request.
 */
void MTCLBackend::send_file(char *, long int, const std::string &) {
    throw std::logic_error("MTCL files must be sent with their request");
}

/**
 * @brief Validates and sends a request with its optional file payload in one frame.
 *
 * A single MTCL message preserves request/file ordering without a second receive operation.
 */
void MTCLBackend::send_request_with_file(const char *message, const int message_len, char *shm,
                                         const long int nbytes, const std::string &target) {
    if (message == nullptr || message_len <= 0 ||
        static_cast<size_t>(message_len) > CAPIO_SERVER_REQUEST_MAX_SIZE || nbytes < 0 ||
        static_cast<uint64_t>(nbytes) > MTCL_MAX_FILE_TRANSFER_SIZE ||
        (nbytes != 0 && shm == nullptr)) {
        throw std::invalid_argument("Invalid MTCL request or file buffer");
    }
    send_frame(message, static_cast<size_t>(message_len), nbytes == 0 ? nullptr : shm,
               static_cast<size_t>(nbytes), target);
}

/**
 * @brief Copies the file retained by read_next_request() into the caller's destination.
 *
 * Matching source and size ensures the payload belongs to the request currently being handled.
 */
void MTCLBackend::recv_file(char *shm, const std::string &source, const long int bytes_expected) {
    if (shm == nullptr || bytes_expected < 0) {
        throw std::invalid_argument("Invalid MTCL destination buffer");
    }
    if (!pending_file || pending_file->source != source ||
        pending_file->frame.size() - pending_file->offset != static_cast<size_t>(bytes_expected)) {
        throw std::runtime_error("MTCL file does not match the pending request");
    }
    std::memcpy(shm, pending_file->frame.data() + pending_file->offset,
                static_cast<size_t>(bytes_expected));
    pending_file.reset();
}

/**
 * @brief Connects to a discovered MTCL endpoint and registers its hostname.
 *
 * Only the lexicographically smaller node initiates the connection, preventing both peers from
 * creating duplicate links. A hostname handshake gives the receiver the same map key.
 */
void MTCLBackend::connect_to(const std::string &target_token) {
    const auto first_colon = target_token.find(':');
    const auto last_colon  = target_token.rfind(':');
    if (first_colon == std::string::npos || first_colon == last_colon) {
        return;
    }
    const std::string remote_hostname =
        target_token.substr(first_colon + 1, last_colon - first_colon - 1);
    if (remote_hostname.empty() || node_name >= remote_hostname) {
        return;
    }
    {
        const std::shared_lock lock(open_connections_lock);
        if (open_connections.count(remote_hostname) != 0) {
            return;
        }
    }

    auto handle = MTCL::Manager::connect(target_token);
    if (!handle.isValid()) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "Unable to connect to %s", target_token.c_str());
        return;
    }
    const size_t hostname_size = node_name.size();
    if (handle.send(&hostname_size, sizeof(hostname_size)) !=
            static_cast<ssize_t>(sizeof(hostname_size)) ||
        handle.send(node_name.data(), hostname_size) != static_cast<ssize_t>(hostname_size)) {
        handle.close();
        return;
    }

    handle.setName(remote_hostname);
    const std::unique_lock lock(open_connections_lock);
    if (open_connections.count(remote_hostname) != 0) {
        handle.close();
        return;
    }
    auto connection = std::make_unique<MTCLConnection>(std::move(handle));
    connection->yield();
    open_connections.emplace(remote_hostname, std::move(connection));
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Connected to %s", target_token.c_str());
}
