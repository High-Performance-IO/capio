#include "harness.hpp"

/*
 * on_close counts any successful close, of a dup'ed or inherited descriptor too, and the implicit
 * closes (exit, close-on-exec, dup2, close_range) of the process that opened the file.
 */
class FsCloseTest : public FsStreamingTest {
  protected:
    /// @brief Run @p producer and return what a consumer that starts 400 ms later reads
    Result consume(std::vector<std::string> producer,
                   const std::chrono::milliseconds timeout = std::chrono::seconds(6)) {
        startServer({{"f"}});
        start("prod", producer);
        pause(400);
        return finish(start("cons", {"open:f", "read"}), timeout);
    }
};

constexpr auto BEFORE_THE_PRODUCER_ENDS = std::chrono::milliseconds(1300);

TEST_F(FsCloseTest, CloseOfADupCounts) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "dup", "close_previous", "sleep:2500"},
                          BEFORE_THE_PRODUCER_ENDS),
                  "A");
}

TEST_F(FsCloseTest, CloseOfAnInheritedCopyCounts) {
    EXPECT_OUTPUT(
        consume({"create:f", "write:A", "fork:close", "sleep:2500"}, BEFORE_THE_PRODUCER_ENDS),
        "A");
}

TEST_F(FsCloseTest, ExitCounts) { EXPECT_OUTPUT(consume({"create:f", "write:A"}), "A"); }

TEST_F(FsCloseTest, Dup2OverTheDescriptorCounts) {
    EXPECT_OUTPUT(
        consume({"create:f", "write:A", "replace", "sleep:2500"}, BEFORE_THE_PRODUCER_ENDS), "A");
}

TEST_F(FsCloseTest, CloseRangeCounts) {
    EXPECT_OUTPUT(
        consume({"create:f", "write:A", "close_range", "sleep:2500"}, BEFORE_THE_PRODUCER_ENDS),
        "A");
}

TEST_F(FsCloseTest, CloseOnExecOfTheOpenerCounts) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "cloexec", "exec:2.5"}, BEFORE_THE_PRODUCER_ENDS),
                  "A");
}

TEST_F(FsCloseTest, DescriptorKeptOverExecCountsAtExit) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "exec:1"}), "A");
}

TEST_F(FsCloseTest, FailedExecIsNoClose) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "cloexec", "exec_missing", "sleep:1200",
                           "write:B", "close"}),
                  "AB");
}

TEST_F(FsCloseTest, EndOfAThreadIsNoClose) {
    EXPECT_OUTPUT(
        consume({"create:f", "write:A", "thread:sleep:10", "sleep:1200", "write:B", "close"}),
        "AB");
}

TEST_F(FsCloseTest, ExitOfAChildWithACopyIsNoClose) {
    EXPECT_OUTPUT(
        consume({"create:f", "write:A", "fork:sleep:10", "sleep:1200", "write:B", "close"}), "AB");
}

TEST_F(FsCloseTest, CloseOnExecInAChildIsNoClose) {
    EXPECT_OUTPUT(
        consume({"create:f", "write:A", "cloexec", "spawn", "sleep:1200", "write:B", "close"}),
        "AB");
}

TEST_F(FsCloseTest, HelperWithACopyIsNoClose) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "spawn", "sleep:1200", "write:B", "close"}),
                  "AB");
}

TEST_F(FsCloseTest, FailedExclusiveCreateCommitsNothing) {
    startServer({{"f", "on_termination"}});
    std::ofstream(work / "f") << "not theirs";
    EXPECT_OUTPUT(run("prod", {"excl:f"}), "excl=EEXIST");
    const auto reader = start("cons", {"open:f", "read"});
    EXPECT_FALSE(finish(reader, std::chrono::milliseconds(1200)).finished);
}

TEST_F(FsCloseTest, ProcessExitEndsItsOtherThreads) {
    startServer({{"f", "on_termination"}});
    EXPECT_OUTPUT(run("prod", {"background:create:f", "sleep:300"}), "");
    EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "");
}
