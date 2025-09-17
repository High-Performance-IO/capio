#include "include/storage-service/capio_storage_service.hpp"
#include "multicast_utils.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <capio/logger.hpp>
#include <include/communication-service/control-plane/multicast_control_plane.hpp>
#include <include/communication-service/data-plane/backend_interface.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

void MulticastControlPlane::multicast_server_aliveness_thread(
    const bool *continue_execution, std::vector<std::string> *token_used_to_connect,
    std::mutex *token_used_to_connect_mutex, int dataplane_backend_port) {

    START_LOG(gettid(), "call(data_plane_backend_port=%d)", dataplane_backend_port);

    char incomingMessage[MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE];

    const std::string SELF_TOKEN = std::string(capio_global_configuration->node_name) + ":" +
                                   std::to_string(dataplane_backend_port);

    sockaddr_in addr  = {};
    socklen_t addrlen = {};
    const auto discovery_socket =
        open_outgoing_socket(MULTICAST_DISCOVERY_ADDR, MULTICAST_DISCOVERY_PORT, addr, addrlen);

    server_println(CAPIO_SERVER_CLI_LOG_SERVER, std::string("Multicast discovery service @ ") +
                                                    MULTICAST_DISCOVERY_ADDR + ":" +
                                                    std::to_string(MULTICAST_DISCOVERY_PORT));

    while (*continue_execution) {
        bzero(incomingMessage, sizeof(incomingMessage));
        // Send port of local data plane backend
        send_multicast_alive_token(dataplane_backend_port);
        LOG("Waiting for incoming token...");

        do {
            const auto recv_sice =
                recvfrom(discovery_socket, incomingMessage, MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE, 0,
                         reinterpret_cast<sockaddr *>(&addr), &addrlen);
            LOG("Received multicast data of size %ld and content %s", recv_sice, incomingMessage);
            if (recv_sice < 0) {
                server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                               std::string("WARNING: received 0 bytes from multicast socket: "));
                server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                               "Execution will continue only with FS discovery support");
                return;
            }
        } while (std::string(incomingMessage) == SELF_TOKEN);

        std::lock_guard lg(*token_used_to_connect_mutex);
        if (std::find(token_used_to_connect->begin(), token_used_to_connect->end(),
                      incomingMessage) == token_used_to_connect->end()) {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                           "Multicast adv: " + std::string(incomingMessage));
            LOG("Received message: %s", incomingMessage);
            token_used_to_connect->push_back(incomingMessage);
            capio_backend->connect_to(incomingMessage);
        }

        sleep(1);
    }
}

void MulticastControlPlane::multicast_control_plane_incoming_thread(
    const bool *continue_execution) {
    START_LOG(gettid(), "Call(multicast_control_plane_incoming_thread)");
    char incoming_msg[MULTICAST_CONTROLPL_MESSAGE_SIZE] = {0};
    sockaddr_in addr                                    = {};
    socklen_t addrlen                                   = {};
    const auto discovery_socket =
        open_outgoing_socket(MULTICAST_CONTROLPL_ADDR, MULTICAST_CONTROLPL_PORT, addr, addrlen);

    server_println(CAPIO_SERVER_CLI_LOG_SERVER, std::string("Multicast control plane @ ") +
                                                    MULTICAST_CONTROLPL_ADDR + ":" +
                                                    std::to_string(MULTICAST_CONTROLPL_PORT));

    while (*continue_execution) {
        bzero(incoming_msg, sizeof(incoming_msg));
        const auto recv_sice =
            recvfrom(discovery_socket, incoming_msg, MULTICAST_CONTROLPL_MESSAGE_SIZE, 0,
                     reinterpret_cast<sockaddr *>(&addr), &addrlen);
        LOG("Received multicast data of size %ld and content %s", recv_sice, incoming_msg);
        if (recv_sice < 0) {
            LOG("WARNING: received size less than zero. An error might have occurred: %s",
                strerror(errno));
            LOG("Skipping iteration and returning to listening for incoming paxkets");
            continue;
        }

        int event;
        char source_hostname[HOST_NAME_MAX];
        char source_path[PATH_MAX];

        sscanf(incoming_msg, "%d %s %s", &event, source_hostname, source_path);

        LOG("event=%d, source=%s, path=%s", event, source_hostname, source_path);

        if (strcmp(capio_global_configuration->node_name, source_hostname) == 0) {
            continue;
        }

        switch (event) {
        case CREATE:
            LOG("Handling remote CREATE event");
            storage_service->createRemoteFile(source_path, source_hostname);
            break;

        default:
            LOG("WARNING: unknown / unhandled event: %s", incoming_msg);
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                           "Unknown/Unhandled message recived: " + std::string(incoming_msg));
        }
        LOG("Completed handling of event");
    }

    close(discovery_socket);
}

MulticastControlPlane::MulticastControlPlane(int dataplane_backend_port) {
    START_LOG(gettid(), "call(dataplane_backend_port=%d)", dataplane_backend_port);
    gethostname(ownHostname, HOST_NAME_MAX);
    continue_execution          = new bool(true);
    token_used_to_connect_mutex = new std::mutex();

    discovery_thread = new std::thread(multicast_server_aliveness_thread, continue_execution,
                                       &token_used_to_connect, token_used_to_connect_mutex,
                                       dataplane_backend_port);

    controlpl_incoming =
        new std::thread(multicast_control_plane_incoming_thread, continue_execution);
}

MulticastControlPlane::~MulticastControlPlane() {
    *continue_execution = false;
    pthread_cancel(discovery_thread->native_handle());
    discovery_thread->join();
    pthread_cancel(controlpl_incoming->native_handle());
    controlpl_incoming->join();
    delete token_used_to_connect_mutex;
    delete discovery_thread;
    delete continue_execution;
    server_println(CAPIO_SERVER_CLI_LOG_SERVER, "MulticastControlPlane correctly terminated");
}

void MulticastControlPlane::notify_all(const event_type event, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(event=%s, path=%s)", event, path.string().c_str());
    sockaddr_in addr = {};
    const auto socket =
        open_outgoing_multicast_socket(MULTICAST_CONTROLPL_ADDR, MULTICAST_CONTROLPL_PORT, &addr);

    char message[MULTICAST_CONTROLPL_MESSAGE_SIZE];
    sprintf(message, "%03d %s %s", event, ownHostname, path.string().c_str());

    LOG("Sending message: %s", message);
    if (sendto(socket, message, strlen(message), 0, reinterpret_cast<sockaddr *>(&addr),
               sizeof(addr)) < 0) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       "WARNING: unable to send message(" + std::string(message) +
                           ") to multicast address!: " + strerror(errno));
    }
    LOG("Sent message");

    close(socket);
}
