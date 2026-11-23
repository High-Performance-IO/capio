#include "harness.hpp"

/*
 * on_close counts any successful close by a producer, of an inherited descriptor too, and the exit
 * of the producer.
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

TEST_F(FsCloseTest, CloseOfAnInheritedCopyCounts) {
    EXPECT_OUTPUT(
        consume({"create:f", "write:A", "fork:close", "sleep:2500"}, BEFORE_THE_PRODUCER_ENDS),
        "A");
}

TEST_F(FsCloseTest, ExitCounts) { EXPECT_OUTPUT(consume({"create:f", "write:A"}), "A"); }

TEST_F(FsCloseTest, EndOfAThreadIsNoClose) {
    EXPECT_OUTPUT(
        consume({"create:f", "write:A", "thread:sleep:10", "sleep:1200", "write:B", "close"}),
        "AB");
}

TEST_F(FsCloseTest, FailedExclusiveCreateCommitsNothing) {
    startServer({{"f", "on_termination"}});
    std::ofstream(work / "f") << "not theirs";
    EXPECT_OUTPUT(run("prod", {"excl:f"}), "excl=EEXIST");
    const auto reader = start("cons", {"open:f", "read"});
    EXPECT_FALSE(finish(reader, std::chrono::milliseconds(1200)).finished);
}
