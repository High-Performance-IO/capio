#include "harness.hpp"

TEST_F(FsStreamingTest, MemoryClientInAnFsWorkflowIsRefused) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"create:f", "write:A", "close"}), "");
    const auto result = finish(start("cons", {"open:f", "read", "print:not refused"},
                                     setting("CAPIO_MEMORY_LIBRARY", "libcapio_posix.so")));
    EXPECT_TRUE(result.finished);
    EXPECT_NE(result.output.find("cannot be mixed"), std::string::npos) << result.output;
    EXPECT_EQ(result.output.find("not refused"), std::string::npos) << result.output;
}

// Without CAPIO_DIR, either library passes every system call to the kernel and needs no server
// (#317): there is none here. The file is on disk even with the memory variant, which would keep a
// CAPIO file in the server.
TEST_F(FsStreamingTest, WithoutCapioDirNothingIsIntercepted) {
    for (const auto &library : {setting("CAPIO_FS_LIBRARY", "libcapio_posix_fs.so"),
                                setting("CAPIO_MEMORY_LIBRARY", "libcapio_posix.so")}) {
        SCOPED_TRACE(library);
        std::filesystem::remove(work / "f");
        const App app{spawn({setting("CAPIO_FS_APP_PATH", "capio_fs_streaming_app"), "create:f",
                             "write:AB", "close", "print:done"},
                            {"CAPIO_DIR", "LD_PRELOAD=" + library}, root / "app", work),
                      root / "app"};
        running.push_back(app.pid);
        EXPECT_OUTPUT(finish(app), "done");
        EXPECT_EQ(slurp(work / "f"), "AB");
    }
}

TEST_F(FsStreamingTest, FsClientInAMemoryWorkflowIsRefused) {
    startServer({{"f"}});
    const auto first =
        finish(start("cons", {"print:up"}, setting("CAPIO_MEMORY_LIBRARY", "libcapio_posix.so")));
    EXPECT_OUTPUT(first, "up");
    // Told at its handshake, before it does anything
    const auto result = run("prod", {"create:f", "write:A", "print:not refused"});
    EXPECT_TRUE(result.finished);
    EXPECT_NE(result.output.find("cannot be mixed"), std::string::npos) << result.output;
    EXPECT_EQ(result.output.find("not refused"), std::string::npos) << result.output;
}
