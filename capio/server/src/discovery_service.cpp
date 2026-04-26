#include <arpa/inet.h>
#include <sys/socket.h>

#include "common/logger.hpp"
#include "remote/backend.hpp"
#include "remote/discovery.hpp"
#include "utils/common.hpp"

extern Backend *backend;

constexpr char CAPIO_MULTICAST_ADDRESS[] = "224.0.0.2";
constexpr int CAPIO_MULTICAST_PORT       = 22334;
int REUSE_MCAST_SOCKET                   = 1;

void advertise(const bool *terminate, const unsigned int delay_ms,
               const std::string &advertisement_token) {
    const int advert_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in advert_multicast_addr{};
    advert_multicast_addr.sin_family      = AF_INET;
    advert_multicast_addr.sin_port        = htons(CAPIO_MULTICAST_PORT);
    advert_multicast_addr.sin_addr.s_addr = inet_addr(CAPIO_MULTICAST_ADDRESS);

    while (!*terminate) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        sendto(advert_sock_fd, advertisement_token.data(), advertisement_token.size(), 0,
               reinterpret_cast<sockaddr *>(&advert_multicast_addr), sizeof(advert_multicast_addr));
    }

    close(advert_sock_fd);
}

void thread_discovery_service(const bool *terminate) {
    START_LOG(gettid(), "call()");

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &REUSE_MCAST_SOCKET, sizeof(REUSE_MCAST_SOCKET));

    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 100000; // 100,000 microseconds = 100ms
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in local_addr{};
    local_addr.sin_family      = AF_INET;
    local_addr.sin_port        = htons(CAPIO_MULTICAST_PORT);
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(sockfd, reinterpret_cast<sockaddr *>(&local_addr), sizeof(local_addr));

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(CAPIO_MULTICAST_ADDRESS);
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

void DiscoveryService::start(unsigned int adv_delay) {
    if (advertisement_token.empty()) {
        throw std::runtime_error("Advertisement token is empty");
    }

    listener_thread = new std::thread(thread_discovery_service, &terminate);
    advertisement_thread =
        new std::thread(advertise, &terminate, adv_delay, std::ref(advertisement_token));

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "DiscoveryService will advertise " +
                                                        advertisement_token + " every " +
                                                        std::to_string(adv_delay) + "ms.");
}

DiscoveryService::~DiscoveryService() {
    terminate = true;

    if (listener_thread->joinable()) {
        listener_thread->join();
        listener_thread = nullptr;
    }

    if (advertisement_thread->joinable()) {
        advertisement_thread->join();
        advertisement_thread = nullptr;
    }
}

void DiscoveryService::setAdvertisementToken(const std::string &token) {
    this->advertisement_token = token;
}
