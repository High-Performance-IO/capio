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
