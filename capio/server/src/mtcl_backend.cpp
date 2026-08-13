#include "common/logger.hpp"
#include "remote/backend/mtcl.hpp"
#include "remote/discovery.hpp"
#include "utils/common.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mtcl.hpp>
#include <stdexcept>

extern DiscoveryService *discovery_service;

struct MTCLConnection::Impl {
    explicit Impl(MTCL::HandleUser connection_handle) : handle(std::move(connection_handle)) {}

    MTCL::HandleUser handle;
    std::mutex send_lock;
    struct PendingSend {
        std::vector<unsigned char> frame;
        MTCL::Request request;
    };
    std::vector<std::unique_ptr<PendingSend>> pending_sends;

    bool cleanup_completed_sends_unlocked() {
        for (auto send = pending_sends.begin(); send != pending_sends.end();) {
            if (!MTCL::test((*send)->request)) {
                ++send;
            } else if ((*send)->request.count() != static_cast<ssize_t>((*send)->frame.size())) {
                return false;
            } else {
                send = pending_sends.erase(send);
            }
        }
        return true;
    }

    ~Impl() {
        pending_sends.clear();
        handle.close();
    }
};

MTCLConnection::MTCLConnection(MTCL::HandleUser handle)
    : impl(std::make_unique<Impl>(std::move(handle))) {}

MTCLConnection::~MTCLConnection() = default;

void MTCLConnection::yield() const { impl->handle.yield(); }

bool MTCLConnection::send_transaction(std::vector<unsigned char> frame) const {
    auto pending   = std::make_unique<Impl::PendingSend>();
    pending->frame = std::move(frame);

    const std::lock_guard lock(impl->send_lock);
    if (!impl->cleanup_completed_sends_unlocked() ||
        impl->handle.isend(pending->frame.data(), pending->frame.size(), pending->request) < 0) {
        return false;
    }
    impl->pending_sends.emplace_back(std::move(pending));
    return true;
}

bool MTCLConnection::cleanup_completed_sends() const {
    const std::lock_guard lock(impl->send_lock);
    return impl->cleanup_completed_sends_unlocked();
}

void write_u64(unsigned char *destination, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        destination[i] = static_cast<unsigned char>(value);
        value >>= 8;
    }
}

uint64_t read_u64(const unsigned char *source) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | source[i];
    }
    return value;
}

bool receive_message(MTCL::HandleUser &handle, void *data, size_t size) {
    return handle.receive(data, size) == static_cast<ssize_t>(size);
}

void discard_message(MTCL::HandleUser &handle) {
    char byte = 0;
    MTCL::Request request;
    if (handle.ireceive(&byte, sizeof(byte), request) == 0) {
        request.wait();
    }
}

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

        if (available < wire_header_size || available > maximum_transaction_size) {
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
            file_size > CAPIO_SERVER_MAX_FILE_TRANSFER_SIZE ||  // file size too big
            (type == MessageType::request && file_size != 0) || // request on file of 0 bytes
            request_size + file_size != available - wire_header_size) { // out of bounds
            remove_connection(remote_hostname);
            continue;
        }

        const auto request_begin = frame.begin() + wire_header_size;
        const auto file_begin    = request_begin + request_size;
        std::string request(request_begin, file_begin);

        if (type == MessageType::request_with_file) {
            if (pending_file) {
                remove_connection(remote_hostname);
                continue;
            }

            pending_file.emplace(PendingFile{remote_hostname, std::move(frame),
                                             wire_header_size + static_cast<size_t>(request_size)});
        }

        handle.yield();
        return {std::move(request), remote_hostname};
    }
    return {std::string{}, std::string{}};
}

void MTCLBackend::accept_connection(MTCL::HandleUser handle) {
    size_t hostname_size = 0;
    if (!receive_message(handle, &hostname_size, sizeof(hostname_size)) || hostname_size == 0 ||
        hostname_size > HOST_NAME_MAX) {
        handle.close();
        return;
    }

    std::string remote_hostname(hostname_size, '\0');
    if (!receive_message(handle, remote_hostname.data(), remote_hostname.size())) {
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
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Connected to " + usedProtocol + ":" +
                                                        remote_hostname + ":" + ownPort +
                                                        " (incoming)");
}

void MTCLBackend::send_transaction(const char *message, size_t message_len, const char *file,
                                   size_t file_len, const std::string &target) {
    std::vector<unsigned char> frame(wire_header_size + message_len + file_len);
    frame[0] = static_cast<unsigned char>(file == nullptr ? MessageType::request
                                                          : MessageType::request_with_file);
    write_u64(frame.data() + 1, message_len);
    write_u64(frame.data() + 9, file_len);
    std::memcpy(frame.data() + wire_header_size, message, message_len);
    if (file_len != 0) {
        std::memcpy(frame.data() + wire_header_size + message_len, file, file_len);
    }

    bool failed = false;
    {
        const std::shared_lock connections_lock(open_connections_lock);
        const auto found = open_connections.find(target);
        if (found == open_connections.end()) {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                           "MTCL connection to " + target + " is not available");
            return;
        }
        auto &connection = *found->second;
        failed           = !connection.send_transaction(std::move(frame));
    }
    if (failed) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "MTCL connection to " + target + " failed");
        remove_connection(target);
    }
}

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

void MTCLBackend::remove_connection(const std::string &hostname) {
    const std::unique_lock lock(open_connections_lock);
    const auto connection = open_connections.find(hostname);
    if (connection == open_connections.end()) {
        return;
    }
    open_connections.erase(connection);
}

MTCLBackend::MTCLBackend(const std::string &proto, const std::string &port, const int sleep_time)
    : Backend(HOST_NAME_MAX), thread_sleep_times(sleep_time),
      listen_token(proto + ":0.0.0.0:" + port),
      advertisement_token(proto + ":" + node_name + ":" + port), ownPort(port),
      usedProtocol(proto) {
    MTCL::Manager::init("server-" + node_name);
    MTCL::Manager::listen(listen_token);
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "MTCL backend listening on " + listen_token);
}

MTCLBackend::~MTCLBackend() {
    continue_execution = false;
    {
        const std::unique_lock lock(open_connections_lock);
        open_connections.clear();
    }
    MTCL::Manager::finalize();
}

void MTCLBackend::handshake_servers() {
    discovery_service->start(advertisement_token, std::max(1, thread_sleep_times / 1000));
}

const std::set<std::string> MTCLBackend::get_nodes() {
    std::set<std::string> nodes{node_name};
    const std::shared_lock lock(open_connections_lock);
    for (const auto &[hostname, connection] : open_connections) {
        nodes.insert(hostname);
    }
    return nodes;
}

void MTCLBackend::send_request(const char *message, const int message_len,
                               const std::string &target) {
    if (message == nullptr || message_len <= 0 ||
        static_cast<size_t>(message_len) > CAPIO_SERVER_REQUEST_MAX_SIZE) {
        throw std::invalid_argument("Invalid MTCL request");
    }
    send_transaction(message, static_cast<size_t>(message_len), nullptr, 0, target);
}

void MTCLBackend::send_file(char *, long int, const std::string &) {
    throw std::logic_error("MTCL files must be sent with their request");
}

void MTCLBackend::send_request_with_file(const char *message, const int message_len, char *shm,
                                         const long int nbytes, const std::string &target) {
    if (message == nullptr || message_len <= 0 ||
        static_cast<size_t>(message_len) > CAPIO_SERVER_REQUEST_MAX_SIZE || nbytes < 0 ||
        static_cast<uint64_t>(nbytes) > CAPIO_SERVER_MAX_FILE_TRANSFER_SIZE ||
        (nbytes != 0 && shm == nullptr)) {
        throw std::invalid_argument("Invalid MTCL request or file buffer");
    }
    send_transaction(message, static_cast<size_t>(message_len), nbytes == 0 ? nullptr : shm,
                     static_cast<size_t>(nbytes), target);
}

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
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "Unable to connect to " + target_token);
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
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Connected to " + target_token);
}
