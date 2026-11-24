#include "harness.hpp"

TEST_F(FsStreamingTest, ListingOfACommittedDirectory) {
    startServer({{"d", "on_n_files", "update", 3}});
    EXPECT_OUTPUT(run("prod", {"mkdir:d", "create:d/a", "close", "create:d/b", "close",
                               "create:d/c", "close"}),
                  "");
    EXPECT_OUTPUT(run("cons", {"list:d"}), "a,b,c,");
}

TEST_F(FsStreamingTest, CommitTokensAreNotEntries) {
    // The end of the producer commits d/a and d/b, which leaves their tokens in d
    startServer({{"d", "on_n_files", "update", 2}});
    EXPECT_OUTPUT(run("prod", {"mkdir:d", "create:d/a", "close", "create:d/b", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"list:d"}), "a,b,");
}

TEST_F(FsStreamingTest, ScandirLists) {
    // It calls readdir inside glibc, where a hook of readdir would not see it
    startServer({{"d", "on_n_files", "no_update", 2}});
    EXPECT_OUTPUT(run("prod", {"mkdir:d", "create:d/a", "close", "create:d/b", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"scandir:d"}), "a,b,");
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
    EXPECT_OUTPUT(run("cons", {"list:plain"}), "x,");
    const auto capio_dir = run("cons", {"list:."});
    EXPECT_TRUE(capio_dir.finished);
    EXPECT_NE(capio_dir.output.find("plain,"), std::string::npos) << capio_dir.output;
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
