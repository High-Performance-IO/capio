#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "connect_recording_backend.hpp"
#include "remote/discovery.hpp"

extern Backend *backend;

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

namespace {

class FsTokenCacheTest : public testing::Test {
  protected:
    std::filesystem::path directory;

    void SetUp() override {
        directory = std::filesystem::temp_directory_path() /
                    ("capio_fs_token_cache_" + std::to_string(getpid()) + "_" +
                     testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
    }

    void TearDown() override { std::filesystem::remove_all(directory); }

    /// @brief Publish @p token as a server does: written aside, then renamed over the old file
    void publish(const std::string &host, const std::string &token) const {
        const auto hidden = directory / ("." + host + ".capio.tmp");
        std::ofstream(hidden) << token;
        std::filesystem::rename(hidden, directory / (host + ".capio"));
    }
};

} // namespace

TEST_F(FsTokenCacheTest, ANewTokenIsFoundOnce) {
    FsTokenCache cache;
    publish("peer", "TCP:peer:1234");
    EXPECT_EQ(cache.scan(directory), std::vector<std::string>{"TCP:peer:1234"});
    EXPECT_TRUE(cache.scan(directory).empty());
    EXPECT_EQ(cache.token(directory / "peer.capio"), "TCP:peer:1234");
    EXPECT_EQ(cache.size(), 1U);
}

TEST_F(FsTokenCacheTest, OnlyPublishedTokenFilesAreRead) {
    FsTokenCache cache;
    std::ofstream(directory / ".peer.capio.tmp") << "TCP:hidden:1";
    std::ofstream(directory / "peer.txt") << "TCP:other:1";
    std::filesystem::create_directory(directory / "sub");
    EXPECT_TRUE(cache.scan(directory).empty());
    EXPECT_EQ(cache.size(), 0U);
}

TEST_F(FsTokenCacheTest, ATokenThatChangedIsReadAgain) {
    // A server restarted on the node publishes another address or port under the same name (#335)
    FsTokenCache cache;
    publish("peer", "TCP:peer:1234");
    EXPECT_EQ(cache.scan(directory), std::vector<std::string>{"TCP:peer:1234"});
    publish("peer", "TCP:peer:5678");
    EXPECT_EQ(cache.scan(directory), std::vector<std::string>{"TCP:peer:5678"});
    EXPECT_EQ(cache.token(directory / "peer.capio"), "TCP:peer:5678");
    EXPECT_TRUE(cache.scan(directory).empty());
}

TEST_F(FsTokenCacheTest, ATokenRewrittenUnchangedIsNotFoundAgain) {
    FsTokenCache cache;
    publish("peer", "TCP:peer:1234");
    EXPECT_EQ(cache.scan(directory).size(), 1U);
    publish("peer", "TCP:peer:1234"); // a new file, with the same token
    EXPECT_TRUE(cache.scan(directory).empty());
}

TEST_F(FsTokenCacheTest, ATokenFileEmptiedKeepsTheTokenReadBefore) {
    FsTokenCache cache;
    publish("peer", "TCP:peer:1234");
    EXPECT_EQ(cache.scan(directory).size(), 1U);
    std::ofstream(directory / "peer.capio").close(); // truncated, being written in place
    EXPECT_TRUE(cache.scan(directory).empty());
    EXPECT_EQ(cache.token(directory / "peer.capio"), "TCP:peer:1234");
    std::ofstream(directory / "peer.capio") << "TCP:peer:1234";
    EXPECT_TRUE(cache.scan(directory).empty()); // the same token once written again
}

TEST_F(FsTokenCacheTest, ARemovedTokenFileIsForgotten) {
    FsTokenCache cache;
    publish("peer", "TCP:peer:1234");
    publish("other", "TCP:other:1234");
    EXPECT_EQ(cache.scan(directory).size(), 2U);
    std::filesystem::remove(directory / "peer.capio");
    EXPECT_TRUE(cache.scan(directory).empty());
    EXPECT_EQ(cache.size(), 1U);
    EXPECT_EQ(cache.token(directory / "peer.capio"), "");
    // If it comes back, its token is found again, even if it is the same
    publish("peer", "TCP:peer:1234");
    EXPECT_EQ(cache.scan(directory), std::vector<std::string>{"TCP:peer:1234"});
}

TEST(FSDiscoveryTest, aPeerThatPublishesANewTokenIsConnectedToAgain) {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("capio_fs_discovery_restart_" + std::to_string(getpid()));
    std::filesystem::remove_all(directory);

    ConnectRecordingBackend peers;
    ASSERT_EQ(backend, nullptr);
    backend = &peers;
    {
        FSDiscoveryInterface discovery(directory.string());
        std::ofstream(directory / "peer.capio") << "TCP:peer:1234";
        discovery.start("TCP:self:1234", 10);

        const auto wait_for = [&peers](const std::string &token) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!peers.connectedTo(token) && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return peers.connectedTo(token);
        };
        EXPECT_TRUE(wait_for("TCP:peer:1234"));

        // The peer restarts on another port, and replaces its token file
        std::ofstream(directory / ".peer.capio.tmp") << "TCP:peer:5678";
        std::filesystem::rename(directory / ".peer.capio.tmp", directory / "peer.capio");
        EXPECT_TRUE(wait_for("TCP:peer:5678")) << "a token that changed was never read again";
        discovery.stop();
    }
    backend = nullptr;
    std::filesystem::remove_all(directory);
}
