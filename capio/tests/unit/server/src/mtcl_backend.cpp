#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <endian.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "child.hpp"
#include "common/requests.hpp"
#include "remote/backend/mtcl.hpp"

/*
 * Peers connect to the address in the token a server advertises. When CAPIO_MTCL_ADVERTISE_HOST
 * names one (e.g. the InfiniBand interface), the token must carry it, followed by the hostname
 * that still identifies the node.
 */
TEST(MTCLBackendTest, advertisesTheAddressSetInCapioMtclAdvertiseHost) {
    setenv("CAPIO_MTCL_ADVERTISE_HOST", "10.1.2.3", 1);
    EXPECT_EQ(make_advertisement_token("TCP", "1234", "node-a"), "TCP:10.1.2.3:1234|node-a");

    unsetenv("CAPIO_MTCL_ADVERTISE_HOST");
    EXPECT_EQ(make_advertisement_token("TCP", "1234", "node-a"), "TCP:node-a:1234");
}

/*
 * The other tests run each server in its own child: MTCL has one manager per process. Both run on
 * this node, so they have the same hostname: the token of the listening one names it "~peer",
 * which sorts after any hostname, so that the other one connects to it.
 */
namespace {

constexpr const char *PEER = "~peer";

std::string free_tcp_port() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    socklen_t length   = sizeof(address);
    bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length);
    close(fd);
    return std::to_string(ntohs(address.sin_port));
}

/// @brief The listening server's token, as connect_to receives it from discovery
std::string peer_token(const std::string &port) { return "TCP:127.0.0.1:" + port + "|" + PEER; }

/// @brief Connects to the peer, which may still be starting
bool connect_to_peer(MTCLBackend &mtcl_backend, const std::string &port) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (mtcl_backend.get_nodes().count(PEER) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        mtcl_backend.connect_to(peer_token(port));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return mtcl_backend.get_nodes().count(PEER) != 0;
}

/// @brief A frame as MTCLBackend sends it: type, request size and file size (big endian), bytes
std::vector<unsigned char> frame(unsigned char type, uint64_t request_size, uint64_t file_size,
                                 const std::string &payload) {
    std::vector<unsigned char> bytes(17 + payload.size());
    bytes[0] = type;
    for (int i = 0; i < 8; ++i) {
        bytes[1 + i] = static_cast<unsigned char>(request_size >> (56 - 8 * i));
        bytes[9 + i] = static_cast<unsigned char>(file_size >> (56 - 8 * i));
    }
    std::memcpy(bytes.data() + 17, payload.data(), payload.size());
    return bytes;
}

/**
 * @brief A connection that sends what a CAPIO server would not, with plain TCP: MTCL frames each
 * message with its size (8 bytes, big endian), and a new connection starts with a 4-byte 0. The
 * test cannot use MTCL itself, which is defined in the headers that mtcl_backend.cpp includes.
 */
class RawConnection {
    int fd = -1;

  public:
    RawConnection(const std::string &port, const std::string &name) {
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(static_cast<uint16_t>(std::stoi(port)));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        // The server may still be starting
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do {
            if (fd >= 0) {
                close(fd);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            fd = socket(AF_INET, SOCK_STREAM, 0);
        } while (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 &&
                 std::chrono::steady_clock::now() < deadline);
        const int handshake = 0;
        send(&handshake, sizeof(handshake));
        if (!name.empty()) {
            const size_t size = name.size();
            send(&size, sizeof(size));
            send(name.data(), size);
        }
    }
    ~RawConnection() { close(fd); }
    RawConnection(const RawConnection &)            = delete;
    RawConnection &operator=(const RawConnection &) = delete;

    void send(const void *data, const size_t size) const {
        const uint64_t header = htobe64(size);
        EXPECT_EQ(write(fd, &header, sizeof(header)), static_cast<ssize_t>(sizeof(header)));
        EXPECT_EQ(write(fd, data, size), static_cast<ssize_t>(size));
    }
};

void let_it_arrive() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }

} // namespace

TEST(MTCLBackendTest, TwoServersExchangeRequestsAndFiles) {
    const std::string port = free_tcp_port();

    // The listening server: answers a request with a file, then waits for the last request
    const pid_t listener = start_child([&] {
            MTCLBackend mtcl_backend("TCP", port, 1000);
            mtcl_backend.handshake_servers(); // the discovery of the unit tests does nothing
            EXPECT_EQ(mtcl_backend.max_file_transfer_size(), 4L * 1024 * 1024 * 1024);

            const RemoteRequest request = mtcl_backend.read_next_request();
            EXPECT_EQ(request.get_code(), CAPIO_SERVER_REQUEST_STAT);
            EXPECT_STREQ(request.get_content(), "a request");
            EXPECT_EQ(request.get_source(), mtcl_backend.get_node_name());

            char file[] = "0123456789";
            mtcl_backend.send_file("0001 a file", 11, file, 10, request.get_source());
            mtcl_backend.send_file("0001 no file", 12, nullptr, 0, request.get_source());

            const RemoteRequest last = mtcl_backend.read_next_request();
            EXPECT_STREQ(last.get_content(), "the last request");
    });

    const int connector = run_in_child([&] {
        MTCLBackend mtcl_backend("TCP", free_tcp_port(), 1000);
        ASSERT_TRUE(connect_to_peer(mtcl_backend, port));
        mtcl_backend.connect_to(peer_token(port)); // already connected: nothing to do
        EXPECT_EQ(mtcl_backend.get_nodes(),
                  (std::set<std::string>{mtcl_backend.get_node_name(), PEER}));

        mtcl_backend.send_request("0002 a request", 14, PEER);
        const RemoteRequest with_file = mtcl_backend.read_next_request();
        EXPECT_EQ(with_file.get_code(), CAPIO_SERVER_REQUEST_READ_REPLY);
        EXPECT_EQ(with_file.get_source(), PEER);
        std::array<char, 10> file{};
        EXPECT_THROW(mtcl_backend.recv_file(file.data(), 9, PEER), std::runtime_error);
        EXPECT_THROW(mtcl_backend.recv_file(nullptr, 10, PEER), std::invalid_argument);
        mtcl_backend.recv_file(file.data(), file.size(), PEER);
        EXPECT_EQ(std::string(file.data(), file.size()), "0123456789");

        const RemoteRequest without_file = mtcl_backend.read_next_request();
        EXPECT_STREQ(without_file.get_content(), "no file");
        EXPECT_THROW(mtcl_backend.recv_file(file.data(), 0, PEER), std::runtime_error);

        mtcl_backend.send_request("0002 the last request", 21, PEER);
        let_it_arrive(); // the send is asynchronous: let it leave before the connection closes
    });
    EXPECT_EQ(connector, 0);

    EXPECT_EQ(wait_child(listener), 0);
}

TEST(MTCLBackendTest, InvalidSendsAreRefused) {
    EXPECT_EQ(run_in_child([] {
                  MTCLBackend mtcl_backend("TCP", free_tcp_port(), 1000);
                  char file[] = "data";
                  EXPECT_THROW(mtcl_backend.send_request(nullptr, 4, PEER), std::invalid_argument);
                  EXPECT_THROW(mtcl_backend.send_request("0002", 0, PEER), std::invalid_argument);
                  EXPECT_THROW(mtcl_backend.send_file("0001", 4, nullptr, 4, PEER),
                               std::invalid_argument);
                  EXPECT_THROW(mtcl_backend.send_file("0001", 4, file, -1, PEER),
                               std::invalid_argument);
                  // To a server it is not connected to: dropped with a warning
                  mtcl_backend.send_request("0002 lost", 9, PEER);
                  mtcl_backend.send_file("0001 lost", 9, file, 4, PEER);
                  // Tokens it cannot use, or of servers that connect to it instead
                  mtcl_backend.connect_to("no-colons");
                  mtcl_backend.connect_to("TCP:1234");
                  mtcl_backend.connect_to("TCP:127.0.0.1:1|");
                  mtcl_backend.connect_to("TCP:127.0.0.1:1|" + mtcl_backend.get_node_name());
                  EXPECT_EQ(mtcl_backend.get_nodes().size(), 1);
              }),
              0);
}

TEST(MTCLBackendTest, APortInUseIsRefused) {
    const std::string port = free_tcp_port();
    const int taken        = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port   = htons(static_cast<uint16_t>(std::stoi(port)));
    ASSERT_EQ(bind(taken, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
    ASSERT_EQ(listen(taken, 1), 0);

    EXPECT_EQ(run_in_child([&] {
                  EXPECT_THROW(MTCLBackend("TCP", port, 1000), std::runtime_error);
              }),
              0);
    close(taken);
}

/*
 * A connection that breaks the protocol is closed, and the server goes on with the others: here
 * it returns only the last request, from a connection that keeps to it.
 */
TEST(MTCLBackendTest, MalformedHandshakesAndFramesCloseTheirConnection) {
    const std::string port = free_tcp_port();

    const pid_t listener = start_child([&] {
            MTCLBackend mtcl_backend("TCP", port, 1000);
            const RemoteRequest request = mtcl_backend.read_next_request();
            EXPECT_STREQ(request.get_content(), "valid");
            EXPECT_EQ(request.get_source(), "good");
    });

    const int client = run_in_child([&] {
        {
            RawConnection no_name(port, "");
            const size_t zero = 0;
            no_name.send(&zero, sizeof(zero)); // a hostname of size 0
            let_it_arrive();
        }

        RawConnection good(port, "good");
        let_it_arrive();
        RawConnection duplicate(port, "good"); // the second connection of a name
        let_it_arrive();

        const std::string request = "0002 bad";
        for (const auto &[name, bytes] :
             std::vector<std::pair<std::string, std::vector<unsigned char>>>{
                 {"short", {1, 2, 3}},                                     // shorter than a header
                 {"type", frame(9, request.size(), 0, request)},           // no such type
                 {"empty", frame(1, 0, 0, "")},                            // no request
                 {"file", frame(2, request.size(), 0, request)},           // a file of size 0
                 {"nofile", frame(1, request.size(), 3, request + "abc")}, // file not announced
                 {"size", frame(1, request.size() + 1, 0, request)},       // sizes do not add up
             }) {
            RawConnection connection(port, name);
            connection.send(bytes.data(), bytes.size());
            let_it_arrive();
        }

        const std::string valid = "0002 valid";
        const auto bytes        = frame(1, valid.size(), 0, valid);
        good.send(bytes.data(), bytes.size());
        let_it_arrive();
    });
    EXPECT_EQ(client, 0);

    EXPECT_EQ(wait_child(listener), 0);
}
