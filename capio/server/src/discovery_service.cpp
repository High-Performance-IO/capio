#include <arpa/inet.h>
#include <sys/socket.h>

#include "common/logger.hpp"
#include "remote/backend.hpp"
#include "remote/discovery.hpp"

extern Backend *backend;

constexpr char CAPIO_MULTICAST_ADDRESS[] = "224.0.0.2";
constexpr int CAPIO_MULTICAST_PORT       = 22334;
constexpr int REUSE_MCAST_SOCKET         = 1;

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

void DiscoveryService::advertise(const std::string &token) {

    const int advert_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in advert_multicast_addr{};
    advert_multicast_addr.sin_family      = AF_INET;
    advert_multicast_addr.sin_port        = htons(CAPIO_MULTICAST_PORT);
    advert_multicast_addr.sin_addr.s_addr = inet_addr(CAPIO_MULTICAST_ADDRESS);

    sendto(advert_sock_fd, token.data(), token.size(), 0,
           reinterpret_cast<sockaddr *>(&advert_multicast_addr), sizeof(advert_multicast_addr));
    close(advert_sock_fd);
}

DiscoveryService::DiscoveryService() {
    listener_thread = new std::thread(thread_discovery_service, &terminate);
}
DiscoveryService::~DiscoveryService() {
    terminate = true;

    pthread_cancel(listener_thread->native_handle());
    listener_thread->join();
}
