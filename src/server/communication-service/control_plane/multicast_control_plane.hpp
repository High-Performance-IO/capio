#ifndef MULTICAST_CONTROL_PLANE_HPP
#define MULTICAST_CONTROL_PLANE_HPP
#include <algorithm>
#include <arpa/inet.h>
#include <capio/logger.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

class MulticastControlPlane : public CapioControlPlane {
    int _backend_port;
    bool *continue_execution;
    std::thread *thread;
    std::vector<std::string> token_used_to_connect;
    std::mutex *token_used_to_connect_mutex;
    char ownHostname[HOST_NAME_MAX] = {0};

    static void send_multicast_alive_token(const int data_plane_backend_port) {
        START_LOG(gettid(), "call(data_plane_backend_port=%d)", data_plane_backend_port);

        int transmission_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (transmission_socket < 0) {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                           std::string("WARNING: unable to bind multicast socket: ") +
                               strerror(errno));
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                           "Execution will continue only with FS discovery support");

            return;
        }

        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = inet_addr(MULTICAST_DISCOVERY_ADDR);
        addr.sin_port        = htons(MULTICAST_DISCOVERY_PORT);

        char message[MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE];
        sprintf(message, "%s:%d", capio_global_configuration->node_name, data_plane_backend_port);

        if (sendto(transmission_socket, message, strlen(message), 0,
                   reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                           "WARNING: unable to send alive token(" + std::string(message) +
                               ") to multicast address!: " + strerror(errno));
        }
        LOG("Sent multicast token");
        close(transmission_socket);
    }

    static void multicast_server_aliveness_thread(const bool *continue_execution,
                                                  std::vector<std::string> *token_used_to_connect,
                                                  std::mutex *token_used_to_connect_mutex,
                                                  const int data_plane_backend_port) {

        START_LOG(gettid(), "call(data_plane_backend_port=%d)", data_plane_backend_port);

        char incomingMessage[MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE];

        int loopback                          = 0; // disable receive loopback messages
        u_int multiple_socket_on_same_address = 1; // enable multiple sockets on same address

        const std::string SELF_TOKEN = std::string(capio_global_configuration->node_name) + ":" +
                                       std::to_string(data_plane_backend_port);

        int discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (discovery_socket < 0) {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                           std::string("WARNING: unable to open multicast socket: ") +
                               strerror(errno));
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                           "Execution will continue only with FS discovery support");
            return;
        }
        LOG("Created socket");

        if (setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR,
                       (char *) &multiple_socket_on_same_address,
                       sizeof(multiple_socket_on_same_address)) < 0) {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                           std::string("WARNING: unable to multiple sockets to same address: ") +
                               strerror(errno));
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                           "Execution will continue only with FS discovery support");
            return;
        }
        LOG("Set IP address to accept multiple sockets on same address");

        if (setsockopt(discovery_socket, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback,
                       sizeof(loopback)) < 0) {
            server_println(
                CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                std::string("WARNING: unable to filter out loopback incoming messages: ") +
                    strerror(errno));
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                           "Execution will continue only with FS discovery support");
            return;
        }
        LOG("Disabled reception of loopback messages from socket");

        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(MULTICAST_DISCOVERY_PORT);
        socklen_t addrlen    = sizeof(addr);
        LOG("Set socket on IP: %s - PORT: %d", MULTICAST_DISCOVERY_ADDR, MULTICAST_DISCOVERY_PORT);

        // bind to receive address
        if (bind(discovery_socket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {

            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                           std::string("WARNING: unable to bind multicast socket: ") +
                               strerror(errno));
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                           "Execution will continue only with FS discovery support");
            return;
        }
        LOG("Binded socket");

        ip_mreq mreq{};
        mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_DISCOVERY_ADDR);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(discovery_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                           std::string("WARNING: unable to join multicast group: ") +
                               strerror(errno));
            server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                           "Execution will continue only with FS discovery support");
            return;
        }
        LOG("Successfully joined multicast group");

        while (*continue_execution) {
            bzero(incomingMessage, sizeof(incomingMessage));
            send_multicast_alive_token(data_plane_backend_port);
            LOG("Waiting for incoming token...");

            do {
                const auto recv_sice =
                    recvfrom(discovery_socket, incomingMessage, MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE,
                             0, reinterpret_cast<sockaddr *>(&addr), &addrlen);
                LOG("Received multicast data of size %ld and content %s", recv_sice,
                    incomingMessage);
                if (recv_sice < 0) {
                    server_println(
                        CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
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

  public:
    explicit MulticastControlPlane(int backend_port) : _backend_port(backend_port) {
        gethostname(ownHostname, HOST_NAME_MAX);
        continue_execution          = new bool(true);
        token_used_to_connect_mutex = new std::mutex();

        thread = new std::thread(multicast_server_aliveness_thread, std::ref(continue_execution),
                                 &token_used_to_connect, token_used_to_connect_mutex, backend_port);

        server_println(CAPIO_SERVER_CLI_LOG_SERVER, std::string("Multicast discovery service @ ") +
                                                        MULTICAST_DISCOVERY_ADDR + ":" +
                                                        std::to_string(MULTICAST_DISCOVERY_PORT));
    }

    ~MulticastControlPlane() override {
        *continue_execution = false;
        pthread_cancel(thread->native_handle());
        thread->join();
        delete token_used_to_connect_mutex;
        delete thread;
        delete continue_execution;
        server_println(CAPIO_SERVER_CLI_LOG_SERVER, "MulticastControlPlane correctly terminated");
    }
};

#endif // MULTICAST_CONTROL_PLANE_HPP
