#include "harness.hpp"

// Without -r, the server streams through the CAPIO_DIR of its first FS client, and refuses an FS
// client with another one
TEST_F(FsStreamingTest, FirstClientGivesTheDirectory) {
    startServer({{"f"}}, false);
    EXPECT_OUTPUT(run("prod", {"create:f", "write:AB", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AB");
    EXPECT_NE(slurp(root / "server.log").find("No -r given"), std::string::npos);

    const auto other = finish(startIn(root / "other", "cons", {"print:not refused"},
                                      setting("CAPIO_FS_LIBRARY", "libcapio_posix_fs.so")));
    EXPECT_TRUE(other.finished);
    EXPECT_NE(other.output.find("Fix CAPIO_DIR"), std::string::npos) << other.output;
    EXPECT_EQ(other.output.find("not refused"), std::string::npos) << other.output;
}

TEST_F(FsStreamingTest, ClientWithAnotherDirectoryThanTheRootIsRefused) {
    startServer({{"f"}});
    const auto other = finish(startIn(root / "other", "prod", {"print:not refused"},
                                      setting("CAPIO_FS_LIBRARY", "libcapio_posix_fs.so")));
    EXPECT_TRUE(other.finished);
    EXPECT_NE(other.output.find("Fix CAPIO_DIR"), std::string::npos) << other.output;
    EXPECT_EQ(other.output.find("not refused"), std::string::npos) << other.output;
}

// Paths are virtual: "f" in one CAPIO_DIR is "f" in any other, as on two nodes that mount the same
// directory at different places. Memory clients may each have their own CAPIO_DIR, which need not
// exist.
TEST_F(FsStreamingTest, MemoryClientsWithDifferentDirectoriesShareFiles) {
    startServer({{"f"}}, false);
    const auto library = setting("CAPIO_MEMORY_LIBRARY", "libcapio_posix.so");
    EXPECT_OUTPUT(
        finish(startIn(root / "mount_a", "prod", {"create:f", "write:AB", "close"}, library)), "");
    EXPECT_OUTPUT(finish(startIn(root / "mount_b", "cons", {"open:f", "read"}, library)), "AB");
    // Nothing reached the disk: there is no -r, and no FS client gave a directory
    EXPECT_FALSE(std::filesystem::exists(root / "mount_a" / "f"));
}
