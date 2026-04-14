#include "common/logger.hpp"
#include "common/requests.hpp"
#include "remote/backend.hpp"
#include "remote/backend/mtcl.hpp"
#include "remote/discovery.hpp"
#include "storage/manager.hpp"
#include "utils/common.hpp"

#include <algorithm>
#include <mtcl.hpp>

// TODO: THERE IS A MASSIVE MEMORY LEAK WHEN SENDING AND RECEIVING CONST CHAR*. FIX IT BEFORE MERGE

// TODO: CLI args (with defaults) instead of hardcoded values

constexpr int max_net_op = 10;

extern Backend *backend;
extern DiscoveryService *discovery_service;
extern StorageManager *storage_service;

RemoteRequest MTCLBackend::read_next_request() {
    START_LOG(gettid(), "call()");

    auto optional_request = incoming_request_queue.try_pop();
    while (!optional_request.has_value()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(thread_sleep_times));
        optional_request = incoming_request_queue.try_pop();
    }

    auto [req, req_size, source] = optional_request.value();
    LOG("Received %s from %d", req.c_str(), source.c_str());
    return {req.data(), source};
}

/**
 * @brief Manages a dedicated P2P connection to a single remote capio_server instance.
 * * The communication logic follows a deterministic role-assignment algorithm:
 * 1. **Initial Role Assignment:** The initial sender is determined by the lexicographical
 * comparison of the two participating hostnames (the smaller hostname starts as sender).
 * 2. **Operational Phases:** The thread executes alternating phases of sending and receiving,
 * processing up to `max_net_op` operations per phase.
 * 3. **Role Switching:** Nodes synchronize a role swap using a `HAVE_FINISH_SEND_REQUEST`
 * signal. This occurs when the current sender either exhausts its message queue or reaches
 * the `max_net_op` threshold.
 * 4. **Termination:** The loop persists as long as the remote handle remains valid and the
 * `terminate` flag is false.
 * @param HandlerPointer A valid MTCL HandlePointer for the connection.
 * @param remote_hostname The hostname of the remote endpoint.
 * @param queue Pointer to the communication hub containing inbound and outbound sub-queues.
 * @param sleep_time Microseconds to sleep between thread cycles to prevent CPU pinning.
 * @param terminate Reference to a heap-allocated boolean controlled by the main thread
 * to signal execution shutdown.
 * @param incoming_request_queue
 */
void serverConnectionHandler(MTCL::HandleUser HandlerPointer, const std::string &remote_hostname,
                             AtomicQueue<const char *> *queue, const int sleep_time,
                             const bool *terminate,
                             AtomicQueue<std::string> *incoming_request_queue) {

    char ownHostname[HOST_NAME_MAX];
    gethostname(ownHostname, HOST_NAME_MAX);
    bool my_turn_to_send = ownHostname > remote_hostname;

    char request_has_finished_to_send[CAPIO_REQ_MAX_SIZE]{0};
    sprintf(request_has_finished_to_send, "%03d", BACKEND_HAVE_FINISH_SEND_REQUEST);

    START_LOG(gettid(), "call(remote_hostname=%s)", remote_hostname.c_str());

    LOG("Will begin execution with %s phase", my_turn_to_send ? "sending" : "receiving");

    while (HandlerPointer.isValid()) {
        // execute up to N operation of send &/or receive, to avoid starvation

        if (my_turn_to_send) {
            LOG("Send PHASE");
            for (int i = 0; i < max_net_op; i++) {
                if (const auto request_opt = queue->try_pop(); request_opt.has_value()) {
                    const auto &[request, request_size, target] = request_opt.value();
                    LOG("Request to be sent = %s to %s", request, target.c_str());

                    HandlerPointer.send(&request_size, sizeof(request_size));
                    HandlerPointer.send(request, request_size);
                }
            }
            LOG("Completed SEND PHASE");
            // Send message I have finished the max number of allowed consecutive io operations
            HandlerPointer.send(request_has_finished_to_send, sizeof(request_has_finished_to_send));

        } else {

            bool continue_receive_phase = true;
            size_t receive_size         = 0;
            LOG("Receive PHASE");
            while (continue_receive_phase) {
                // Receive phase
                HandlerPointer.probe(receive_size, false);
                if (receive_size > 0) {
                    LOG("A request is incoming");

                    ssize_t incoming_request_size = 0;
                    HandlerPointer.receive(&incoming_request_size, sizeof(incoming_request_size));

                    const auto incoming_request = new char[incoming_request_size];
                    const auto resp_size =
                        HandlerPointer.receive(incoming_request, incoming_request_size);
                    LOG("Received request with size = %ld", incoming_request_size);

                    if (const auto code =
                            RemoteRequest{incoming_request, remote_hostname}.get_code();
                        code == BACKEND_HAVE_FINISH_SEND_REQUEST) {
                        // Finished sending data. Set continue_receive_phase = false to go to next
                        // phase
                        LOG("CTRL MSG received: Other has finished sending phase. Switching me "
                            "from receive to send");
                        continue_receive_phase = false;
                    } else {
                        incoming_request_queue->push(incoming_request, resp_size, remote_hostname);
                    }
                }
            }
        }

        // terminate phase
        if (*terminate) {
            LOG("[TERM PHASE] Closing connection");
            HandlerPointer.close();
            LOG("[TERM PHASE] Terminating thread server_connection_handler");
            return;
        }

        my_turn_to_send = !my_turn_to_send;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
    }
}

void MTCLBackend::incomingMTCLConnectionListener(
    const std::string &ownHostname, const std::string &ownPort, const std::string &usedProtocol,
    const bool *continue_execution, int sleep_time,
    std::unordered_map<std::string, AtomicQueue<const char *> *> *open_connections,
    std::mutex *guard, std::vector<std::thread *> *_connection_threads, bool *terminate,
    AtomicQueue<std::string> *incoming_request_queue) {

    std::string selfToken = usedProtocol + ":" + ownHostname + ":" + ownPort;

    START_LOG(gettid(), "call(sleep_time=%d)", sleep_time);

    while (*continue_execution) {

        if (auto UserManager = MTCL::Manager::getNext(std::chrono::microseconds(sleep_time));
            UserManager.isValid()) {
            // received MTCL handle
            LOG("Handle user is valid");
            char connected_hostname[HOST_NAME_MAX] = {0};
            UserManager.receive(connected_hostname, HOST_NAME_MAX);
            LOG("Received connection hostname: %s", connected_hostname);

            auto *queue = new AtomicQueue<const char *>();
            {
                const std::lock_guard lock(*guard);
                open_connections->insert({connected_hostname, queue});
            }
            _connection_threads->push_back(
                new std::thread(serverConnectionHandler, std::move(UserManager), connected_hostname,
                                queue, sleep_time, terminate, incoming_request_queue));
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Connected to " + usedProtocol + ":" +
                                                                std::string(connected_hostname) +
                                                                ":" + ownPort);
        } else {
            // broadcast ADV on multicast of me being alive by sending token named selfToken
            DiscoveryService::advertise(selfToken);
        }
    }
}

MTCLBackend::MTCLBackend(const std::string &proto, const std::string &port, const int sleep_time)
    : Backend(HOST_NAME_MAX), selfToken(proto + ":0.0.0.0:" + port), ownPort(port),
      usedProtocol(proto), thread_sleep_times(sleep_time) {
    START_LOG(gettid(), "INFO: instance of MTCLBackend");

    terminate  = new bool;
    *terminate = false;

    _guard = new std::mutex();

    ownHostname.resize(HOST_NAME_MAX, '\0');
    gethostname(ownHostname.data(), HOST_NAME_MAX);
    ownHostname.resize(strnlen(ownHostname.c_str(), HOST_NAME_MAX));

    LOG("My hostname is %s. Starting to listen on connection %s", ownHostname.c_str(),
        selfToken.c_str());

    std::string hostname_id("server-");
    hostname_id += ownHostname;
    MTCL::Manager::init(hostname_id);

    *continue_execution = true;

    MTCL::Manager::listen(selfToken);

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "MTCL_backend initialization completed.");
}

void MTCLBackend::handshake_servers() {
    incoming_MTCL_connection_listener_thread =
        new std::thread(incomingMTCLConnectionListener, ownHostname, ownPort, usedProtocol,
                        std::ref(continue_execution), thread_sleep_times, &open_connections, _guard,
                        &connection_threads, terminate, &incoming_request_queue);
}

MTCLBackend::~MTCLBackend() {
    START_LOG(gettid(), "call()");
    *terminate          = true;
    *continue_execution = false;

    for (const auto thread : connection_threads) {
        thread->join();
    }
    LOG("Terminated connection threads");

    pthread_cancel(incoming_MTCL_connection_listener_thread->native_handle());
    incoming_MTCL_connection_listener_thread->join();

    delete incoming_MTCL_connection_listener_thread;
    delete continue_execution;
    delete terminate;

    LOG("Handler closed.");

    MTCL::Manager::finalize();
    LOG("Finalizing MTCL backend");
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "MTCL backend cleanup completed.");
}

const std::set<std::string> MTCLBackend::get_nodes() {
    std::set<std::string> keys;
    for (const auto &[hostname, _handle] : open_connections) {
        keys.insert(hostname);
    }

    return keys;
}

void MTCLBackend::send_request(const char *message, const int message_len,
                               const std::string &target) {

    START_LOG(gettid(), "call(target=%s, message=%s, message_len=%ld)", target.c_str(), message,
              message_len);

    const auto queues = open_connections.at(target);
    LOG("obtained access to queue");

    queues->push(message, message_len, target);
    LOG("Request pushed to output queue");
}

void MTCLBackend::send_file(char *shm, long int nbytes, const std::string &target) {
    START_LOG(gettid(), "call(target=%s, nbytes=%ld)", target.c_str(), nbytes);

    const auto queue = open_connections.at(target);
    queue->push(shm, nbytes, target);
}

void MTCLBackend::recv_file(char *shm, const std::string &source, long int bytes_expected) {
    const auto queues = open_connections.at(source);
    const auto data   = queues->pop();
    memcpy(shm, data.object, bytes_expected);
}

void MTCLBackend::connect_to(const std::string &target_token) {
    START_LOG(gettid(), "call(target=%s)", target_token.c_str());

    if (std::string(target_token) == selfToken) {
        LOG("Skipping to connect to self");
        return;
    }

    const std::string hostname_port(target_token);

    std::string remoteHostname = hostname_port.substr(
        hostname_port.find(':') + 1,                                  // Drop proto
        hostname_port.find_last_of(':') - hostname_port.find(':') - 1 // drop port
    );

    /*
     * Connect to remote only if its hostname is lexically smaller than self hostname
     * If current server hostname is equal to remoteHostname, avoid connection
     * TODO: extend this to support also different workflows on same nodes. (NB: right now we expect
             different MCAST groups )
     */
    if (ownHostname >= remoteHostname) {
        return;
    }

    if (open_connections.find(remoteHostname) != open_connections.end()) {
        LOG("Remote host %s is already connected", remoteHostname.c_str());
        return;
    }

    if (auto UserManager = MTCL::Manager::connect(target_token); UserManager.isValid()) {
        LOG("Opened connection with: %s", target_token.c_str());

        // send my hostname
        char _ownHotname_cstr[PATH_MAX]{0};
        sprintf(_ownHotname_cstr, "%s", ownHostname.c_str());
        UserManager.send(_ownHotname_cstr, HOST_NAME_MAX);

        auto *queue = new AtomicQueue<const char *>();
        {
            const std::lock_guard lg(*_guard);
            open_connections.insert({remoteHostname, queue});
        }
        connection_threads.push_back(
            new std::thread(serverConnectionHandler, std::move(UserManager), remoteHostname, queue,
                            thread_sleep_times, terminate, &incoming_request_queue));
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Connected to " + target_token);
    } else {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "Warning: tried to connect to " +
                                                               std::string(remoteHostname) +
                                                               " but connection is not valid");
    }
}
