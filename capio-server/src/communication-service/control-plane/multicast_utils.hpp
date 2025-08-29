#ifndef CAPIO_MULTICAST_UTILS_HPP
#define CAPIO_MULTICAST_UTILS_HPP

#include <arpa/inet.h>
#include <capio/logger.hpp>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <utils/configuration.hpp>

static int open_outgoing_multicast_socket(const char *address, const int port, sockaddr_in *addr) {
    int transmission_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (transmission_socket < 0) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       std::string("WARNING: unable to bind multicast socket: ") + strerror(errno));
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "Execution will continue only with FS discovery support");

        return -1;
    }

    addr->sin_family      = AF_INET;
    addr->sin_addr.s_addr = inet_addr(address);
    addr->sin_port        = htons(port);
    return transmission_socket;
};

static void send_multicast_alive_token(const int data_plane_backend_port) {
    START_LOG(gettid(), "call(data_plane_backend_port=%d)", data_plane_backend_port);

    sockaddr_in addr = {};
    const auto socket =
        open_outgoing_multicast_socket(MULTICAST_DISCOVERY_ADDR, MULTICAST_DISCOVERY_PORT, &addr);

    char message[MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE];
    sprintf(message, "%s:%d", capio_global_configuration->node_name, data_plane_backend_port);

    LOG("Sending token: %s", message);

    if (sendto(socket, message, strlen(message), 0, reinterpret_cast<sockaddr *>(&addr),
               sizeof(addr)) < 0) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       "WARNING: unable to send alive token(" + std::string(message) +
                           ") to multicast address!: " + strerror(errno));
    }
    LOG("Sent multicast token");
    close(socket);
}

static int open_outgoing_socket(const char *address_ip, const int port, sockaddr_in &addr,
                                socklen_t &addrlen) {
    START_LOG(gettid(), "call(address=%s, port=%d)", address_ip, port);
    int loopback                          = 0; // disable receive loopback messages
    u_int multiple_socket_on_same_address = 1; // enable multiple sockets on same address

    int outgoing_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (outgoing_socket < 0) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       std::string("WARNING: unable to open multicast socket: ") + strerror(errno));
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "Execution will continue only with FS discovery support");
        return -1;
    }
    LOG("Created socket");

    if (setsockopt(outgoing_socket, SOL_SOCKET, SO_REUSEADDR,
                   (char *) &multiple_socket_on_same_address,
                   sizeof(multiple_socket_on_same_address)) < 0) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       std::string("WARNING: unable to multiple sockets to same address: ") +
                           strerror(errno));
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "Execution will continue only with FS discovery support");
        return -1;
    }
    LOG("Set IP address to accept multiple sockets on same address");

    if (setsockopt(outgoing_socket, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) <
        0) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       std::string("WARNING: unable to filter out loopback incoming messages: ") +
                           strerror(errno));
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "Execution will continue only with FS discovery support");
        return -1;
    }
    LOG("Disabled reception of loopback messages from socket");

    addr                 = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    addrlen              = sizeof(addr);
    LOG("Set socket on IP: %s - PORT: %d", address_ip, port);

    // bind to receive address
    if (bind(outgoing_socket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {

        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       std::string("WARNING: unable to bind multicast socket: ") + strerror(errno));
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "Execution will continue only with FS discovery support");
        return -1;
    }
    LOG("Binded socket");

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(address_ip);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(outgoing_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       std::string("WARNING: unable to join multicast group: ") + strerror(errno));
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                       "Execution will continue only with FS discovery support");
        return -1;
    }
    LOG("Successfully joined multicast group");
    return outgoing_socket;
}

#endif // CAPIO_MULTICAST_UTILS_HPP