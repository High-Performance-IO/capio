#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "remote/backend.hpp"
#include "remote/discovery.hpp"

extern Backend *backend;

namespace {

// Records the tokens the discovery service asks to connect to.
class ConnectRecordingBackend : public Backend {
    std::mutex _mutex;
    std::vector<std::string> _tokens;

  public:
    ConnectRecordingBackend() : Backend(256) {}

    void handshake_servers() override {}
    RemoteRequest read_next_request() override { return {std::string{}, std::string{}}; }
    void send_file(const char *, int, char *, long int, const std::string &) override {}
    void recv_file(char *, long int, const std::string &) override {}
    void send_request(const char *, int, const std::string &) override {}
    void connect_to(const std::string &token) override {
        const std::lock_guard lg(_mutex);
        _tokens.push_back(token);
    }

    bool connectedTo(const std::string &token) {
        const std::lock_guard lg(_mutex);
        return std::find(_tokens.begin(), _tokens.end(), token) != _tokens.end();
    }
};

} // namespace

/*
 * The token file of a peer can be listed before its content is written: a server that writes it
 * in place on a shared file system creates it empty first. The discovery service must read it
 * again at a later scan, not ignore that peer forever.
 */
TEST(FSDiscoveryTest, tokenFileFoundEmptyIsReadAgainOnceWritten) {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("capio_fs_discovery_test_" + std::to_string(getpid()));
    std::filesystem::remove_all(directory);

    ConnectRecordingBackend peers;
    ASSERT_EQ(backend, nullptr);
    backend = &peers;
    {
        FSDiscoveryInterface discovery(directory.string());
        const auto peer_token_file = directory / "peer.capio";
        std::ofstream(peer_token_file).close();

        discovery.start("TCP:self:1234", 10);
        // Let several scans find the peer's token file still empty.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::ofstream(peer_token_file) << "TCP:peer:1234";

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!peers.connectedTo("TCP:peer:1234") && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_TRUE(peers.connectedTo("TCP:peer:1234"))
            << "a token file found empty was never read again";
        discovery.stop();
    }
    backend = nullptr;
    std::filesystem::remove_all(directory);
}
