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

    static void send_multicast_alive_token(const int backend_port) {
        char hostname[HOST_NAME_MAX];
        gethostname(hostname, HOST_NAME_MAX);

        int transmission_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (transmission_socket < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << hostname << " ] "
                      << "WARNING: unable to bind multicast socket: " << strerror(errno)
                      << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << hostname << " ] "
                      << "Execution will continue only with FS discovery support" << std::endl;
            return;
        }

        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = inet_addr(MULTICAST_DISCOVERY_ADDR);
        addr.sin_port        = htons(MULTICAST_DISCOVERY_PORT);

        char message[MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE];
        sprintf(message, "%s:%d", hostname, backend_port);

        if (sendto(transmission_socket, message, strlen(message), 0,
                   reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << hostname << " ] "
                      << "WARNING: unable to send alive token(" << message
                      << ") to multicast address!: " << strerror(errno) << std::endl;
        }
        close(transmission_socket);
    }

    static void multicast_server_aliveness_detector_thread(
        const bool *continue_execution, std::vector<std::string> *token_used_to_connect,
        std::mutex *token_used_to_connect_mutex, const int backend_port) {

        char incomingMessage[MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE];
        char ownHostname[HOST_NAME_MAX];
        gethostname(ownHostname, HOST_NAME_MAX);

        int discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (discovery_socket < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                      << "WARNING: unable to open multicast socket: " << strerror(errno)
                      << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                      << "Execution will continue only with FS discovery support" << std::endl;
            return;
        }

        u_int multiple_socket_on_same_address = 1;
        if (setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR,
                       (char *) &multiple_socket_on_same_address,
                       sizeof(multiple_socket_on_same_address)) < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                      << "WARNING: unable to assign multiple sockets to same address: "
                      << strerror(errno) << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                      << "Execution will continue only with FS discovery support" << std::endl;
            return;
        }

        struct sockaddr_in addr = {};
        addr.sin_family         = AF_INET;
        addr.sin_addr.s_addr    = htonl(INADDR_ANY);
        addr.sin_port           = htons(MULTICAST_DISCOVERY_PORT);
        socklen_t addrlen       = sizeof(addr);

        // bind to receive address
        if (bind(discovery_socket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {

            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                      << "WARNING: unable to bind multicast socket: " << strerror(errno)
                      << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                      << "Execution will continue only with FS discovery support" << std::endl;
            return;
        }

        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_DISCOVERY_ADDR);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(discovery_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                      << "WARNING: unable to join multicast group: " << strerror(errno)
                      << std::endl;
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                      << "Execution will continue only with FS discovery support" << std::endl;
            return;
        }

        while (*continue_execution) {
            bzero(incomingMessage, sizeof(incomingMessage));
            send_multicast_alive_token(backend_port);
            if (recvfrom(discovery_socket, incomingMessage, MULTICAST_ALIVE_TOKEN_MESSAGE_SIZE, 0,
                         reinterpret_cast<sockaddr *>(&addr), &addrlen) < 0) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << ownHostname << " ] "
                          << "WARNING: recvied < 0 bytes from multicast: " << strerror(errno)
                          << std::endl;
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                          << "Execution will continue only with FS discovery support" << std::endl;
                return;
            }

            if (std::string(incomingMessage) ==
                std::string(ownHostname) + ":" + std::to_string(backend_port)) {
                // skip myself
                continue;
            }

            std::lock_guard lg(*token_used_to_connect_mutex);
            if (std::find(token_used_to_connect->begin(), token_used_to_connect->end(),
                          incomingMessage) == token_used_to_connect->end()) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << ownHostname << " ] "
                          << "Multicast adv: " << incomingMessage << std::endl;
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

        thread = new std::thread(multicast_server_aliveness_detector_thread,
                                 std::ref(continue_execution), &token_used_to_connect,
                                 token_used_to_connect_mutex, backend_port);
    }

    ~MulticastControlPlane() override {
        *continue_execution = false;
        pthread_cancel(thread->native_handle());
        thread->join();
        delete token_used_to_connect_mutex;
        delete thread;
        delete continue_execution;
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << ownHostname << " ] "
                  << "MulticastControlPlane correctly terminated" << std::endl;
    }
};

#endif // MULTICAST_CONTROL_PLANE_HPP
