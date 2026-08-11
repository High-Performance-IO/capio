#include "common/logger.hpp"
#include "remote/backend/mtcl.hpp"
#include "remote/discovery.hpp"
#include "utils/common.hpp"

#include <cstring>
#include <mtcl.hpp>
#include <stdexcept>

constexpr int max_net_op = 10;

enum class MessageType : unsigned char { request = 1, file = 2, turn = 3 };

extern DiscoveryService *discovery_service;

RemoteRequest MTCLBackend::read_next_request() {
    START_LOG(gettid(), "call()");
    while (continue_execution) {
        if (auto request = incoming_request_queue.try_pop()) {
            return {std::move(request->object), std::move(request->target_or_source)};
        }
        std::this_thread::sleep_for(std::chrono::microseconds(thread_sleep_times));
    }
    return {std::string{}, std::string{}};
}

static bool receive_payload(MTCL::HandleUser &handle, std::vector<char> &payload,
                            size_t maximum_size) {
    size_t size = 0;
    if (handle.receive(&size, sizeof(size)) != static_cast<ssize_t>(sizeof(size)) ||
        size > maximum_size) {
        return false;
    }
    payload.resize(size);
    return size == 0 || handle.receive(payload.data(), size) == static_cast<ssize_t>(size);
}

static void serverConnectionHandler(MTCL::HandleUser handle, const std::string &remote_hostname,
                                    MTCLConnection *connection, const int sleep_time,
                                    const std::atomic_bool *continue_execution,
                                    AtomicQueue<std::string> *incoming_request_queue) {
    char own_hostname[HOST_NAME_MAX]{};
    gethostname(own_hostname, sizeof(own_hostname));
    bool my_turn_to_send = std::string(own_hostname) > remote_hostname;

    while (handle.isValid() && *continue_execution) {
        if (my_turn_to_send) {
            for (int i = 0; i < max_net_op; ++i) {
                auto queued = connection->outgoing.try_pop();
                if (!queued) {
                    break;
                }
                auto &frame = queued->object;
                const auto type = static_cast<unsigned char>(frame.front());
                const size_t size = frame.size() - 1;
                handle.send(&type, sizeof(type));
                handle.send(&size, sizeof(size));
                if (size > 0) {
                    handle.send(frame.data() + 1, size);
                }
            }
            const auto turn = static_cast<unsigned char>(MessageType::turn);
            handle.send(&turn, sizeof(turn));
        } else {
            bool receiving = true;
            while (receiving && handle.isValid() && *continue_execution) {
                size_t available = 0;
                handle.probe(available, false);
                if (available == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(sleep_time));
                    continue;
                }

                unsigned char raw_type = 0;
                if (handle.receive(&raw_type, sizeof(raw_type)) != sizeof(raw_type)) {
                    handle.close();
                    return;
                }
                const auto type = static_cast<MessageType>(raw_type);
                if (type == MessageType::turn) {
                    receiving = false;
                    continue;
                }

                std::vector<char> payload;
                const size_t maximum = type == MessageType::request
                                           ? CAPIO_SERVER_REQUEST_MAX_SIZE
                                           : static_cast<size_t>(CAPIO_DEFAULT_FILE_INITIAL_SIZE);
                if ((type != MessageType::request && type != MessageType::file) ||
                    !receive_payload(handle, payload, maximum)) {
                    handle.close();
                    return;
                }

                if (type == MessageType::request) {
                    const size_t size = payload.size();
                    incoming_request_queue->push(
                        std::string(payload.begin(), payload.end()), size, remote_hostname);
                } else {
                    const size_t size = payload.size();
                    connection->incoming_files.push(std::move(payload), size, remote_hostname);
                }
            }
        }

        my_turn_to_send = !my_turn_to_send;
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_time));
    }
    handle.close();
}

void MTCLBackend::incomingMTCLConnectionListener(
    const std::string &ownPort, const std::string &usedProtocol,
    const std::atomic_bool *continue_execution, int sleep_time,
    std::unordered_map<std::string, std::unique_ptr<MTCLConnection>> *open_connections,
    std::shared_mutex *open_connection_guard, std::mutex *connection_threads_guard,
    std::vector<std::thread> *connection_threads,
    AtomicQueue<std::string> *incoming_request_queue) {
    while (*continue_execution) {
        auto handle = MTCL::Manager::getNext(std::chrono::microseconds(sleep_time));
        if (!handle.isValid()) {
            continue;
        }

        size_t hostname_size = 0;
        if (handle.receive(&hostname_size, sizeof(hostname_size)) != sizeof(hostname_size) ||
            hostname_size == 0 || hostname_size > HOST_NAME_MAX) {
            handle.close();
            continue;
        }
        std::string remote_hostname(hostname_size, '\0');
        if (handle.receive(remote_hostname.data(), hostname_size) !=
            static_cast<ssize_t>(hostname_size)) {
            handle.close();
            continue;
        }

        MTCLConnection *connection;
        {
            const std::unique_lock lock(*open_connection_guard);
            if (open_connections->count(remote_hostname) != 0) {
                handle.close();
                continue;
            }
            connection = open_connections->emplace(remote_hostname,
                                                   std::make_unique<MTCLConnection>())
                             .first->second.get();
        }
        {
            const std::lock_guard lock(*connection_threads_guard);
            connection_threads->emplace_back(serverConnectionHandler, std::move(handle),
                                             remote_hostname, connection, sleep_time,
                                             continue_execution, incoming_request_queue);
        }
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Connected to " + usedProtocol + ":" +
                                                            remote_hostname + ":" + ownPort +
                                                            " (incoming)");
    }
}

MTCLBackend::MTCLBackend(const std::string &proto, const std::string &port, const int sleep_time)
    : Backend(HOST_NAME_MAX), thread_sleep_times(sleep_time),
      listen_token(proto + ":0.0.0.0:" + port),
      advertisement_token(proto + ":" + node_name + ":" + port), ownPort(port),
      usedProtocol(proto) {
    std::string hostname_id = "server-" + node_name;
    MTCL::Manager::init(hostname_id);
    MTCL::Manager::listen(listen_token);
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "MTCL backend listening on " + listen_token);
}

MTCLBackend::~MTCLBackend() {
    continue_execution = false;
    if (incoming_connection_thread.joinable()) {
        incoming_connection_thread.join();
    }
    {
        const std::lock_guard lock(connection_threads_lock);
        for (auto &thread : connection_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    open_connections.clear();
    MTCL::Manager::finalize();
}

void MTCLBackend::handshake_servers() {
    if (incoming_connection_thread.joinable()) {
        return;
    }
    incoming_connection_thread = std::thread(
        incomingMTCLConnectionListener, ownPort, usedProtocol, &continue_execution,
        thread_sleep_times, &open_connections, &open_connections_lock, &connection_threads_lock,
        &connection_threads, &incoming_request_queue);
    discovery_service->start(advertisement_token,
                             std::max(1, thread_sleep_times / 1000));
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
    std::vector<char> frame(static_cast<size_t>(message_len) + 1);
    frame.front() = static_cast<char>(MessageType::request);
    std::memcpy(frame.data() + 1, message, static_cast<size_t>(message_len));
    const std::shared_lock lock(open_connections_lock);
    open_connections.at(target)->outgoing.push(std::move(frame), message_len, target);
}

void MTCLBackend::send_file(char *shm, long int nbytes, const std::string &target) {
    if (shm == nullptr || nbytes < 0 || nbytes > CAPIO_DEFAULT_FILE_INITIAL_SIZE) {
        throw std::invalid_argument("Invalid MTCL file buffer");
    }
    std::vector<char> frame(static_cast<size_t>(nbytes) + 1);
    frame.front() = static_cast<char>(MessageType::file);
    std::memcpy(frame.data() + 1, shm, static_cast<size_t>(nbytes));
    const std::shared_lock lock(open_connections_lock);
    open_connections.at(target)->outgoing.push(std::move(frame), static_cast<size_t>(nbytes), target);
}

void MTCLBackend::recv_file(char *shm, const std::string &source, long int bytes_expected) {
    if (shm == nullptr || bytes_expected < 0) {
        throw std::invalid_argument("Invalid MTCL destination buffer");
    }
    MTCLConnection *connection;
    {
        const std::shared_lock lock(open_connections_lock);
        connection = open_connections.at(source).get();
    }
    auto data = connection->incoming_files.pop();
    if (!data || data->object.size() != static_cast<size_t>(bytes_expected)) {
        throw std::runtime_error("MTCL file size does not match the expected size");
    }
    std::memcpy(shm, data->object.data(), static_cast<size_t>(bytes_expected));
}

void MTCLBackend::connect_to(const std::string &target_token) {
    const auto first_colon = target_token.find(':');
    const auto last_colon = target_token.rfind(':');
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
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "Unable to connect to " + target_token);
        return;
    }
    const size_t hostname_size = node_name.size();
    handle.send(&hostname_size, sizeof(hostname_size));
    handle.send(node_name.data(), hostname_size);

    MTCLConnection *connection;
    {
        const std::unique_lock lock(open_connections_lock);
        if (open_connections.count(remote_hostname) != 0) {
            handle.close();
            return;
        }
        connection = open_connections.emplace(remote_hostname, std::make_unique<MTCLConnection>())
                         .first->second.get();
    }
    {
        const std::lock_guard lock(connection_threads_lock);
        connection_threads.emplace_back(serverConnectionHandler, std::move(handle), remote_hostname,
                                        connection, thread_sleep_times, &continue_execution,
                                        &incoming_request_queue);
    }
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Connected to " + target_token);
}
