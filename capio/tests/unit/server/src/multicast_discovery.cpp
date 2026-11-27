#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "connect_recording_backend.hpp"
#include "remote/discovery.hpp"

extern Backend *backend;

namespace {

/// @brief A UDP socket bound to a free port of 127.0.0.1, without SO_REUSEADDR
int bound_udp_socket(unsigned int &port) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t length        = sizeof(address);
    bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length);
    port = ntohs(address.sin_port);
    return fd;
}

bool connects_within(ConnectRecordingBackend &peers, const std::string &token,
                     const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!peers.connectedTo(token) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return peers.connectedTo(token);
}

} // namespace

/*
 * The listener takes every token it receives on the port, its own among them: the backend ignores
 * that one. The advertisements go to 127.0.0.1 here, which needs no multicast route.
 */
TEST(MulticastDiscoveryTest, AnAdvertisedTokenReachesTheBackend) {
    unsigned int port;
    close(bound_udp_socket(port));

    ConnectRecordingBackend peers;
    ASSERT_EQ(backend, nullptr);
    backend = &peers;
    {
        MulticastDiscoveryInterface discovery("127.0.0.1", port);
        discovery.start("TCP:self:1234", 10);
        EXPECT_TRUE(connects_within(peers, "TCP:self:1234", std::chrono::seconds(5)));
        discovery.stop();
        discovery.stop(); // a second stop has no thread left to join
    }
    backend = nullptr;
}

TEST(MulticastDiscoveryTest, AListenerThatCannotBindStopsAlone) {
    unsigned int port;
    const int taken = bound_udp_socket(port); // without SO_REUSEADDR: no one else can bind it

    ConnectRecordingBackend peers;
    ASSERT_EQ(backend, nullptr);
    backend = &peers;
    {
        MulticastDiscoveryInterface discovery("127.0.0.1", port);
        discovery.start("TCP:self:1234", 10);
        EXPECT_FALSE(connects_within(peers, "TCP:self:1234", std::chrono::milliseconds(200)));
        discovery.stop();
    }
    backend = nullptr;
    close(taken);
}
