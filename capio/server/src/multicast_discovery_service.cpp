
#include <arpa/inet.h>
#include <sys/socket.h>

#include "common/logger.hpp"
#include "remote/backend.hpp"
#include "remote/discovery.hpp"
#include "server/include/remote/discovery.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"

extern Backend *backend;

// constant required by setsockopt()
int REUSE_MCAST_SOCKET = 1;

void advertise(const bool *terminate, const unsigned int delay_ms,
               const std::string &advertisement_token, const std::string &adv_addr,
               const unsigned int adv_port) {
    const int advert_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in advert_multicast_addr{};
    advert_multicast_addr.sin_family      = AF_INET;
    advert_multicast_addr.sin_port        = htons(adv_port);
    advert_multicast_addr.sin_addr.s_addr = inet_addr(adv_addr.c_str());

    while (!*terminate) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        sendto(advert_sock_fd, advertisement_token.data(), advertisement_token.size(), 0,
               reinterpret_cast<sockaddr *>(&advert_multicast_addr), sizeof(advert_multicast_addr));
    }

    close(advert_sock_fd);
}

void mcast_thread_discovery_service(const bool *terminate, const std::string &adv_addr,
                                    const unsigned int adv_port) {
    START_LOG(gettid(), "call()");

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &REUSE_MCAST_SOCKET, sizeof(REUSE_MCAST_SOCKET));

    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 100000; // 100,000 microseconds = 100ms
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in local_addr{};
    local_addr.sin_family      = AF_INET;
    local_addr.sin_port        = htons(adv_port);
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, reinterpret_cast<sockaddr *>(&local_addr), sizeof(local_addr)) == -1) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       "Error: unable to bind to multicast socket. Error is: " +
                           std::string(std::strerror(errno)));
        // halt execution and return
        return;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(adv_addr.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    char incoming_token[2 * HOST_NAME_MAX] = {0};

    while (!*terminate) {

        bzero(incoming_token, 2 * HOST_NAME_MAX);

        if (recvfrom(sockfd, incoming_token, sizeof(incoming_token) - 1, 0, nullptr, nullptr) > 0) {
            backend->connect_to(incoming_token);
        }
    }
    close(sockfd);
}

void MulticastDiscoveryInterface::stop() {
    terminate = true;

    if (mcast_listener_thread != nullptr && mcast_listener_thread->joinable()) {
        mcast_listener_thread->join();
        mcast_listener_thread = nullptr;
        server_println("Multicast listener service stopped.",
                       CapioCLEngine::get().getWorkflowName(), CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                       "MulticastDiscoveryService");
    }

    if (advertisement_thread != nullptr && advertisement_thread->joinable()) {
        advertisement_thread->join();
        advertisement_thread = nullptr;
        server_println("Multicast advertisement service stopped.",
                       CapioCLEngine::get().getWorkflowName(), CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                       "MulticastDiscoveryService");
    }
}

MulticastDiscoveryInterface::~MulticastDiscoveryInterface() = default;

MulticastDiscoveryInterface::MulticastDiscoveryInterface(const std::string &mcast_addr,
                                                         unsigned int mcast_port)
    : capio_multicast_adv_address(mcast_addr), capio_multicast_adv_port(mcast_port) {}

void MulticastDiscoveryInterface::start(const std::string &token, unsigned int adv_delay) {
    mcast_listener_thread = new std::thread(mcast_thread_discovery_service, &terminate,
                                            capio_multicast_adv_address, capio_multicast_adv_port);

    advertisement_thread = new std::thread(advertise, &terminate, adv_delay, token,
                                           capio_multicast_adv_address, capio_multicast_adv_port);
}
