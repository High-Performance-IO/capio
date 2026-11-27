#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>

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
