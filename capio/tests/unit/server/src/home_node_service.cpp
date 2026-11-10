#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "remote/backend.hpp"
#include "remote/handlers/location.hpp"
#include "remote/home_node_service.hpp"

extern Backend *backend;
extern HomeNodeService *home_node_service;

namespace {

// Two other servers, peer-a and peer-b, whose requests the tests record and answer by hand.
class PeersBackend : public Backend {
    std::mutex _mutex;
    std::vector<std::pair<std::string, std::string>> _sent; ///< (message, target)

  public:
    PeersBackend() : Backend(256) {}

    const std::set<std::string> get_nodes() override { return {node_name, "peer-a", "peer-b"}; }
    void handshake_servers() override {}
    RemoteRequest read_next_request() override { return {std::string{}, std::string{}}; }
    void send_file(const char *, int, char *, long int, const std::string &) override {}
    void recv_file(char *, long int, const std::string &) override {}
    void connect_to(const std::string &) override {}
    void send_request(const char *message, int, const std::string &target) override {
        const std::lock_guard lg(_mutex);
        _sent.emplace_back(message, target);
    }

    std::vector<std::pair<std::string, std::string>> sent() {
        const std::lock_guard lg(_mutex);
        return _sent;
    }

    void clear() {
        const std::lock_guard lg(_mutex);
        _sent.clear();
    }

    // Wait until at least n requests were sent
    bool waitForSent(std::size_t n) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (sent().size() < n) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
};

// The query identifier of a "0008 <id> <path>" request
long queryId(const std::string &message) { return std::stol(message.substr(5)); }

} // namespace

class HomeNodeServiceTest : public testing::Test {
  protected:
    PeersBackend peers;
    // A long timeout: lookups must end on the answers, not on the timeout
    HomeNodeService service{std::chrono::seconds(30), std::chrono::seconds(30)};
    HomeNodeService *global_service = nullptr;

    void SetUp() override {
        ASSERT_EQ(backend, nullptr);
        backend           = &peers;
        global_service    = home_node_service;
        home_node_service = &service;
    }

    void TearDown() override {
        home_node_service = global_service;
        backend           = nullptr;
    }

    std::string self() const { return peers.get_node_name(); }
};

TEST_F(HomeNodeServiceTest, creatingAFileAnnouncesItToEveryOtherServer) {
    service.setHome("/capio/file");

    EXPECT_EQ(service.getHome("/capio/file"), self());
    const auto sent = peers.sent();
    ASSERT_EQ(sent.size(), 2);
    EXPECT_EQ(sent[0].first, "0006 /capio/file");
    EXPECT_EQ(sent[1].first, "0006 /capio/file");
    EXPECT_NE(sent[0].second, sent[1].second);
    EXPECT_NE(sent[0].second, self());
    EXPECT_NE(sent[1].second, self());
}

TEST_F(HomeNodeServiceTest, lookupOfAKnownFileAsksNoOne) {
    service.learn("/capio/file", "peer-a");

    EXPECT_EQ(service.lookup("/capio/file"), "peer-a");
    EXPECT_TRUE(peers.sent().empty());
}

TEST_F(HomeNodeServiceTest, lookupOfAFileNoServerHoldsEndsWhenEveryServerAnswered) {
    auto result = std::async(std::launch::async, [this] { return service.lookup("/capio/new"); });

    ASSERT_TRUE(peers.waitForSent(2));
    const auto sent = peers.sent();
    EXPECT_EQ(sent[0].first.substr(0, 5), "0008 ");
    const long id = queryId(sent[0].first);
    EXPECT_EQ(queryId(sent[1].first), id);

    service.receiveAnswer(id, false, "peer-a");
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
    service.receiveAnswer(id, false, "peer-b");

    ASSERT_EQ(result.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_FALSE(result.get());
    EXPECT_FALSE(service.getHome("/capio/new"));
}

TEST_F(HomeNodeServiceTest, lookupReturnsTheServerThatHoldsTheFile) {
    auto result = std::async(std::launch::async, [this] { return service.lookup("/capio/file"); });

    ASSERT_TRUE(peers.waitForSent(2));
    service.receiveAnswer(queryId(peers.sent()[0].first), true, "peer-b");

    ASSERT_EQ(result.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(result.get(), "peer-b");
    EXPECT_EQ(service.getHome("/capio/file"), "peer-b");
}

TEST_F(HomeNodeServiceTest, waitForAFileEndsWhenItsCreationIsAnnounced) {
    auto result =
        std::async(std::launch::async, [this] { return service.waitForHome("/capio/later"); });

    ASSERT_TRUE(peers.waitForSent(2));
    const long id = queryId(peers.sent()[0].first);
    service.receiveAnswer(id, false, "peer-a");
    service.receiveAnswer(id, false, "peer-b");
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);

    service.learn("/capio/later", "peer-a");

    ASSERT_EQ(result.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(result.get(), "peer-a");
}

TEST_F(HomeNodeServiceTest, onlyTheHomeOfAFileSaysItHoldsIt) {
    service.setHome("/capio/mine");
    service.learn("/capio/theirs", "peer-a");
    peers.clear();

    service.answerQuery(7, "/capio/mine", "peer-b");
    service.answerQuery(8, "/capio/theirs", "peer-b");
    service.answerQuery(9, "/capio/unknown", "peer-b");

    const auto sent = peers.sent();
    ASSERT_EQ(sent.size(), 3);
    EXPECT_EQ(sent[0], std::make_pair(std::string("0009 7 1 /capio/mine"), std::string("peer-b")));
    EXPECT_EQ(sent[1],
              std::make_pair(std::string("0009 8 0 /capio/theirs"), std::string("peer-b")));
    EXPECT_EQ(sent[2],
              std::make_pair(std::string("0009 9 0 /capio/unknown"), std::string("peer-b")));
}

TEST_F(HomeNodeServiceTest, removingAFileThisServerHoldsTellsTheOthers) {
    service.setHome("/capio/mine");
    service.learn("/capio/theirs", "peer-a");
    peers.clear();

    EXPECT_EQ(service.remove("/capio/mine"), 1);
    EXPECT_EQ(service.remove("/capio/theirs"), 1);
    EXPECT_EQ(service.remove("/capio/unknown"), 2);

    const auto sent = peers.sent();
    ASSERT_EQ(sent.size(), 2);
    EXPECT_EQ(sent[0].first, "0007 /capio/mine");
    EXPECT_EQ(sent[1].first, "0007 /capio/mine");
    EXPECT_FALSE(service.getHome("/capio/mine"));
    EXPECT_FALSE(service.getHome("/capio/theirs"));
}

TEST_F(HomeNodeServiceTest, renamingAFileMovesItsAnnouncement) {
    service.setHome("/capio/old");
    peers.clear();

    service.rename("/capio/old", "/capio/new");

    EXPECT_FALSE(service.getHome("/capio/old"));
    EXPECT_EQ(service.getHome("/capio/new"), self());
    const auto sent = peers.sent();
    ASSERT_EQ(sent.size(), 4);
    EXPECT_EQ(sent[0].first, "0007 /capio/old");
    EXPECT_EQ(sent[2].first, "0006 /capio/new");
}

TEST_F(HomeNodeServiceTest, forgettingAFileThatMovedKeepsItsNewHome) {
    // peer-a removes the file and peer-b creates it again, but peer-b's announcement arrives first
    service.learn("/capio/moved", "peer-a");
    service.learn("/capio/moved", "peer-b");
    service.forget("/capio/moved", "peer-a");
    EXPECT_EQ(service.getHome("/capio/moved"), "peer-b");

    service.forget("/capio/moved", "peer-b");
    EXPECT_FALSE(service.getHome("/capio/moved"));
}

TEST_F(HomeNodeServiceTest, anotherServerAnnouncingAPathDoesNotTakeItFromThisOne) {
    service.setHome("/capio/dir");
    service.learn("/capio/dir", "peer-a");
    EXPECT_EQ(service.getHome("/capio/dir"), self());
}

TEST_F(HomeNodeServiceTest, remoteRequestsReachTheService) {
    remote_location_announce_handler(
        RemoteRequest(std::string("0006 /capio/with space"), std::string("peer-a")));
    EXPECT_EQ(service.getHome("/capio/with space"), "peer-a");

    service.setHome("/capio/mine too");
    peers.clear();
    remote_location_query_handler(
        RemoteRequest(std::string("0008 42 /capio/mine too"), std::string("peer-b")));
    const auto sent = peers.sent();
    ASSERT_EQ(sent.size(), 1);
    EXPECT_EQ(sent[0].first, "0009 42 1 /capio/mine too");
    EXPECT_EQ(sent[0].second, "peer-b");

    remote_location_forget_handler(
        RemoteRequest(std::string("0007 /capio/with space"), std::string("peer-a")));
    EXPECT_FALSE(service.getHome("/capio/with space"));
}
