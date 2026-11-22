#include "harness.hpp"

TEST_F(FsStreamingTest, ListingInUpdateWaitsForNFiles) {
    startServer({{"d", "on_n_files", "update", 3}});
    start("prod", {"mkdir:d", "create:d/a", "close", "create:d/b", "close", "sleep:1000",
                   "create:d/c", "close", "sleep:1000"});
    pause(300);
    EXPECT_OUTPUT(run("cons", {"list:d"}), "a,b,c,");
}

TEST_F(FsStreamingTest, ListingInNoUpdateStreams) {
    startServer({{"d", "on_n_files", "no_update", 2}});
    start("prod", {"mkdir:d", "create:d/a", "close", "sleep:2500", "create:d/b", "close"});
    pause(300);
    EXPECT_OUTPUT(finish(start("cons", {"list_first:d"}), std::chrono::milliseconds(1200)), "a");
    EXPECT_OUTPUT(run("cons", {"list:d"}), "a,b,");
}

TEST_F(FsStreamingTest, ScandirWaitsToo) {
    // It calls readdir inside glibc, where a hook of readdir would not see it
    startServer({{"d", "on_n_files", "no_update", 2}});
    start("prod", {"mkdir:d", "create:d/a", "close", "sleep:1000", "create:d/b", "close"});
    pause(300);
    EXPECT_OUTPUT(run("cons", {"scandir:d"}), "a,b,");
}

TEST_F(FsStreamingTest, NFilesDefaultsToOneWithoutDotEntries) {
    // The schema of CAPIO-CL 2.0.3 rejects on_n_files without n_files. Its parser takes 0 as unset.
    startServer({{"d", "on_n_files", "update", 0}});
    const auto reader = start("cons", {"list:d"});
    EXPECT_OUTPUT(run("prod", {"mkdir:d"}), "");
    EXPECT_FALSE(finish(reader, std::chrono::milliseconds(700)).finished) << "committed empty";
    EXPECT_OUTPUT(run("prod", {"create:d/a", "close"}), "");
    EXPECT_OUTPUT(finish(reader), "a,");
}

TEST_F(FsStreamingTest, RenamedEntriesCount) {
    startServer({{"d", "on_n_files", "update", 1}});
    EXPECT_OUTPUT(run("prod", {"mkdir:d", "create:t", "close", "rename:t>d/a"}), "");
    EXPECT_OUTPUT(run("cons", {"list:d"}), "a,");
}

TEST_F(FsStreamingTest, ProducerListsWithoutWaiting) {
    startServer({{"d", "on_n_files", "update", 3}});
    EXPECT_OUTPUT(run("prod", {"mkdir:d", "create:d/a", "close", "list:d"}), "a,");
}

TEST_F(FsStreamingTest, OtherDirectoriesListWithoutWaiting) {
    startServer({{"f"}});
    std::filesystem::create_directory(work / "plain");
    std::ofstream(work / "plain" / "x") << "x";
    EXPECT_OUTPUT(run("cons", {"list:plain", "list:."}), "x,plain,");
}

TEST_F(FsStreamingTest, RewindListsAgain) {
    startServer({{"d", "on_n_files", "update", 2}});
    EXPECT_OUTPUT(run("prod", {"mkdir:d", "create:d/a", "close", "create:d/b", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"list_twice:d"}), "22");
}

TEST_F(FsStreamingTest, SeekdirReturnsToAnEntry) {
    startServer({{"d", "on_n_files", "update", 2}});
    EXPECT_OUTPUT(run("prod", {"mkdir:d", "create:d/a", "close", "create:d/b", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"list_seek:d"}), "same");
}

TEST_F(FsStreamingTest, ManyEntriesSpanSeveralBuffers) {
    constexpr int N_ENTRIES = 1500;
    startServer({{"d", "on_n_files", "update", N_ENTRIES}});
    std::filesystem::create_directory(work / "d");
    for (int i = 0; i < N_ENTRIES; ++i) {
        std::ofstream(work / "d" / ("a_rather_long_name_of_an_entry_" + std::to_string(i)));
    }
    EXPECT_OUTPUT(run("cons", {"list_twice:d"}), "15001500");
}
