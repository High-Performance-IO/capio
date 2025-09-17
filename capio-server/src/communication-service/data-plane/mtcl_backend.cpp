#include "include/file-manager/file_manager.hpp"
#include "include/storage-service/capio_storage_service.hpp"

#include <algorithm>
#include <capio/logger.hpp>
#include <include/communication-service/data-plane/mtcl_backend.hpp>
#include <include/utils/configuration.hpp>
#include <mtcl.hpp>

/**
 * This thread will handle connections towards a single target.
 */
void MTCLBackend::serverConnectionHandler(MTCL::HandleUser HandlerPointer,
                                          const std::string &remote_hostname, MessageQueue *queue,
                                          const int sleep_time, const bool *terminate,
                                          const CONN_HANDLER_ORIGIN source) {
    constexpr int max_net_op = 10;
    /*
     * Algorithm works in this way. At the beginning, the role of the node that starts to send is
     * chosen as the smaller lexicographically between the two hostnames involved in the
     * communication. Then two phases of sending and receiving up to max_net_op are performed.
     * The two nodes switches phases after a final synchronization with the special request
     * HAVE_FINISH_SEND_REQUEST.
     * When the sender sends this request, either because it has reached the max_net_op or because
     * there are no more messages to be sent, the two nodes switches roles. This continues until the
     * remote handle pointer is valid
     */

    char ownHostname[HOST_NAME_MAX];
    gethostname(ownHostname, HOST_NAME_MAX);
    bool my_turn_to_send = ownHostname > remote_hostname;

    char request_has_finished_to_send[CAPIO_REQ_MAX_SIZE]{0};
    sprintf(request_has_finished_to_send, "%03d", HAVE_FINISH_SEND_REQUEST);

    START_LOG(gettid(), "call(remote_hostname=%s, kind=%s)", remote_hostname.c_str(),
              source == FROM_REMOTE ? "from remote server" : "to remote server");

    LOG("Will begin execution with %s phase", my_turn_to_send ? "sending" : "receiving");

    while (HandlerPointer.isValid()) {
        // execute up to N operation of send &/or receive, to avoid starvation

        if (my_turn_to_send) {
            for (int i = 0; i < max_net_op && queue->has_requests(); i++) {
                // Send of request
                auto request = queue->get_request();
                HandlerPointer.send(request.c_str(), request.length());

                // Retrive size of response
                capio_off64_t response_size;
                HandlerPointer.receive(&response_size, sizeof(response_size));
                char *response_buffer = new char[response_size];
                HandlerPointer.receive(response_buffer, response_size);

                // push response back to the source
                queue->push_response(response_buffer, response_size, request);
            }

            // Send message I have finished
            HandlerPointer.send(request_has_finished_to_send, sizeof(request_has_finished_to_send));

        } else {
            for (int i = 0; i < max_net_op; i++) {
                // Receive phase
                size_t receive_size = 0;
                HandlerPointer.probe(receive_size, false);
                while (receive_size > 0) {
                    int requestCode;
                    char incoming_request[CAPIO_REQ_MAX_SIZE];
                    HandlerPointer.receive(incoming_request, receive_size);
                    sscanf(incoming_request, "%d", &requestCode);

                    switch (requestCode) {
                    case HAVE_FINISH_SEND_REQUEST: {
                        // Finished sending data. Set i to be greater than max_net_op to go to next
                        // phase
                        i = max_net_op;
                        break;
                    }

                    case FETCH_FROM_REMOTE: {
                        // Scan request fetch from remote
                        char filepath[PATH_MAX];
                        capio_off64_t offset, count;

                        sscanf(incoming_request, "%s %llu %llu", filepath, &offset, &count);

                        auto buffer = new char[count];
                        auto read_size =
                            storage_service->readFromFileToBuffer(filepath, offset, buffer, count);
                        HandlerPointer.send(&read_size, sizeof(read_size));
                        HandlerPointer.send(buffer, read_size);
                        delete[] buffer;
                        break;
                    }

                    default:
                        break;
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

void MTCLBackend::incomingConnectionListener(
    const bool *continue_execution, int sleep_time,
    std::unordered_map<std::string, MessageQueue *> *open_connections, std::mutex *guard,
    std::vector<std::thread *> *_connection_threads, bool *terminate) {

    char ownHostname[HOST_NAME_MAX] = {0};
    gethostname(ownHostname, HOST_NAME_MAX);

    START_LOG(gettid(), "call(sleep_time=%d, hostname=%s)", sleep_time, ownHostname);

    while (*continue_execution) {
        auto UserManager = MTCL::Manager::getNext(std::chrono::microseconds(sleep_time));

        if (!UserManager.isValid()) {
            continue;
        }
        LOG("Handle user is valid");
        char connected_hostname[HOST_NAME_MAX] = {0};
        UserManager.receive(connected_hostname, HOST_NAME_MAX);

        server_println(CAPIO_SERVER_CLI_LOG_SERVER,
                       std::string("Connected from ") + connected_hostname);

        LOG("Received connection hostname: %s", connected_hostname);

        const std::lock_guard lock(*guard);

        open_connections->insert({connected_hostname, {}});

        _connection_threads->push_back(new std::thread(
            serverConnectionHandler, std::move(UserManager), connected_hostname,
            open_connections->at(connected_hostname), sleep_time, terminate, FROM_REMOTE));
    }
}

void MTCLBackend::connect_to(std::string hostname_port) {
    START_LOG(gettid(), "call( hostname_port=%s)", hostname_port.c_str());
    std::string remoteHost        = hostname_port.substr(0, hostname_port.find_last_of(':'));
    const std::string remoteToken = usedProtocol + ":" + hostname_port;

    if (remoteToken == selfToken ||                                     // skip on 0.0.0.0
        remoteToken == usedProtocol + ":" + ownHostname + ":" + ownPort // skip on my real IP
    ) {
        LOG("Skipping to connect to self");
        return;
    }

    if (open_connections.contains(remoteHost)) {
        LOG("Remote host %s is already connected", remoteHost.c_str());
        return;
    }

    LOG("Trying to connect on remote: %s", remoteToken.c_str());
    if (auto UserManager = MTCL::Manager::connect(remoteToken); UserManager.isValid()) {
        server_println(CAPIO_SERVER_CLI_LOG_SERVER, std::string("Connected to ") + remoteToken);
        LOG("Connected to: %s", remoteToken.c_str());
        UserManager.send(ownHostname, HOST_NAME_MAX);
        const std::lock_guard lg(*_guard);

        open_connections.insert({remoteHost, {}});

        auto queue_elem = open_connections.at(remoteHost);

        connection_threads.push_back(new std::thread(serverConnectionHandler,
                                                     std::move(UserManager), remoteHost, queue_elem,
                                                     thread_sleep_times, terminate, TO_REMOTE));
    } else {
        server_println(CAPIO_SERVER_CLI_LOG_SERVER_WARNING, "Warning: tried to connect to " +
                                                                std::string(remoteHost) +
                                                                " but connection is not valid");
    }
}

MTCLBackend::MTCLBackend(const std::string &proto, const std::string &port, int sleep_time)
    : selfToken(proto + ":0.0.0.0:" + port), ownPort(port), usedProtocol(proto),
      thread_sleep_times(sleep_time) {
    START_LOG(gettid(), "INFO: instance of CapioCommunicationService");

    terminate  = new bool;
    *terminate = false;

    _guard = new std::mutex();

    gethostname(ownHostname, HOST_NAME_MAX);
    LOG("My hostname is %s. Starting to listen on connection %s", ownHostname, selfToken.c_str());

    std::string hostname_id("server-");
    hostname_id += ownHostname;
    MTCL::Manager::init(hostname_id);

    *continue_execution = true;

    MTCL::Manager::listen(selfToken);

    th = new std::thread(incomingConnectionListener, std::ref(continue_execution), sleep_time,
                         &open_connections, _guard, &connection_threads, terminate);
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "MTCL_backend initialization completed.");
}

MTCLBackend::~MTCLBackend() {
    START_LOG(gettid(), "call()");
    *terminate          = true;
    *continue_execution = false;

    for (const auto thread : connection_threads) {
        thread->join();
    }
    LOG("Terminated connection threads");

    pthread_cancel(th->native_handle());
    th->join();
    delete th;
    delete continue_execution;
    delete terminate;

    LOG("Handler closed.");

    MTCL::Manager::finalize();
    LOG("Finalizing MTCL backend");
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "MTCL_backend cleanup completed.");
}

std::vector<std::string> MTCLBackend::get_open_connections() {
    std::vector<std::string> keys;
    keys.reserve(open_connections.size()); // avoid reallocations

    for (const auto &pair : open_connections) {
        keys.push_back(pair.first);
    }

    return keys;
}

size_t MTCLBackend::fetchFromRemoteHost(const std::string &hostname,
                                        const std::filesystem::path &filepath, char *buffer,
                                        capio_off64_t offset, capio_off64_t count) {

    START_LOG(gettid(), "call(hostname=%s, path=%s, offset=%ld, count=%ld)", hostname.c_str(),
              filepath.c_str(), offset, count);

    char REQUEST[CAPIO_REQ_MAX_SIZE];

    sprintf(REQUEST, "%03d %s %llu %llu", FETCH_FROM_REMOTE, filepath.c_str(), offset, count);
    LOG("Sending request %s", REQUEST);
    auto queues = open_connections.at(hostname);
    LOG("obtained access to queue");

    queues->push_request(REQUEST);
    LOG("Request pushed to output queue");
    auto [buff_size, response_buffer] = queues->get_response();
    LOG("Obtained response. Buffer size of response is $ld", buff_size);
    storage_service->storeData(filepath, offset, buff_size, response_buffer);

    return 0;
}
