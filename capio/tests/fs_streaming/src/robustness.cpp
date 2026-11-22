#include "harness.hpp"

TEST_F(FsStreamingTest, RequestsOfThreadsKilledByExitDoNotStopTheServer) {
    // exit_group is announced before the kernel ends the other threads, which still send requests
    startServer({{"f"}});
    std::ofstream(work / "plain") << "x";
    for (int i = 0; i < 15; ++i) {
        ASSERT_TRUE(run("other", {"background:hammer:plain", "background:hammer:plain",
                                  "background:hammer:plain", "sleep:30"})
                        .finished);
        ASSERT_EQ(waitpid(server, nullptr, WNOHANG), 0) << "the server stopped, round " << i;
    }
    EXPECT_OUTPUT(run("other", {"stat:plain"}), "1");
}

TEST_F(FsStreamingTest, ThreadsCanOpenAndCloseTogether) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"pool:400", "print:done"}), "done");
}
