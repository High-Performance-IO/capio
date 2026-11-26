#include "harness.hpp"

/*
 * on_close counts the closes of a producer, also of its copies of a descriptor, and the implicit
 * ones: the exit, close-on-exec, dup2 over the descriptor and close_range.
 */
class FsCloseTest : public FsStreamingTest {
  protected:
    void SetUp() override {
        FsStreamingTest::SetUp();
        startServer({{"f"}});
    }

    /// @brief Run @p producer, then return what a consumer reads
    Result consume(const std::vector<std::string> &producer) {
        EXPECT_OUTPUT(run("prod", producer), "");
        return run("cons", {"open:f", "read"});
    }
};

TEST_F(FsCloseTest, ExitCounts) { EXPECT_OUTPUT(consume({"create:f", "write:A"}), "A"); }

TEST_F(FsCloseTest, CopyOfADescriptorWrites) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "dup", "close_previous", "write:B", "close"}),
                  "AB");
}

TEST_F(FsCloseTest, Dup2OverTheDescriptorCounts) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "replace"}), "A");
}

TEST_F(FsCloseTest, CloseRangeCounts) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "close_range"}), "A");
}

TEST_F(FsCloseTest, CloseOnExecCounts) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "cloexec", "exec"}), "A");
}

TEST_F(FsCloseTest, DescriptorKeptOverExecCounts) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "exec"}), "A");
}

TEST_F(FsCloseTest, FailedExecKeepsTheDescriptor) {
    EXPECT_OUTPUT(consume({"create:f", "write:A", "cloexec", "exec_missing", "write:B", "close"}),
                  "AB");
}

TEST_F(FsStreamingTest, CloseInAForkedChildCounts) {
    startServer({{"f", "on_close:2"}});
    EXPECT_OUTPUT(run("prod", {"create:f", "write:A", "fork:close", "write:B", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AB");
}

TEST_F(FsStreamingTest, AnotherThreadClosesADescriptorOfItsProcess) {
    // The number of f is free again after the thread closes it, and g gets it
    startServer({{"f"}, {"g"}});
    EXPECT_OUTPUT(
        run("prod", {"create:f", "write:A", "thread:close", "create:g", "write:B", "print:done"}),
        "done");
    EXPECT_OUTPUT(run("cons", {"open:f", "read", "open:g", "read"}), "AB");
}
