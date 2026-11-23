#include "harness.hpp"

TEST_F(FsStreamingTest, CountedCloseNeedsNoMetadata) {
    startServer({{"f", "on_close:2"}});
    EXPECT_OUTPUT(
        run("prod", {"create:f", "write:AAAA", "close", "append:f", "write:BBBB", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AAAABBBB");
}

TEST_F(FsStreamingTest, CommitsLeaveNoTokens) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"create:f", "write:AAAA", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AAAA");
    for (const auto &entry : std::filesystem::directory_iterator(work)) {
        // Not just "f": builds with logging put capio_logs there
        const auto name = entry.path().filename().string();
        EXPECT_EQ(name.find(".commit"), std::string::npos) << name;
        EXPECT_NE(name, "capiocl");
    }
}

TEST_F(FsStreamingTest, ClosesDoNotStallTheServer) {
    std::vector<Stream> streams;
    std::vector<std::string> operations;
    for (int i = 0; i < 20; ++i) {
        streams.push_back({"out" + std::to_string(i)});
        operations.push_back("create:out" + std::to_string(i));
        operations.emplace_back("close");
    }
    startServer(streams);
    EXPECT_OUTPUT(run("prod", operations), "");
    EXPECT_OUTPUT(finish(start("cons", {"open:out19", "read"}), std::chrono::milliseconds(1500)),
                  "");
}

TEST_F(FsStreamingTest, WaitingReadersDoNotStallTheServer) {
    // A check on the request thread costs 300 ms per waiter, 6 s for 20: the bound leaves room
    // for slow runners (aarch64, Debug with coverage)
    constexpr int n_waiters = 20;
    std::vector<Stream> streams{{"other"}};
    for (int i = 0; i < n_waiters; ++i) {
        streams.push_back({"in" + std::to_string(i)});
        std::ofstream(work / ("in" + std::to_string(i))) << "partial";
    }
    startServer(streams);
    for (int i = 0; i < n_waiters; ++i) {
        start("cons", {"open:in" + std::to_string(i), "read"});
    }
    pause(100);
    EXPECT_OUTPUT(finish(start("prod", {"create:other", "close", "open:other", "read"}),
                         std::chrono::milliseconds(3000)),
                  "");
}
