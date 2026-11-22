#include "harness.hpp"

/*
 * A consumer that starts while the producer is halfway must get all the data, whatever call it
 * uses to get it.
 */
class FsWaitingTest : public FsStreamingTest, public testing::WithParamInterface<const char *> {
  protected:
    void SetUp() override {
        FsStreamingTest::SetUp();
        startServer({{"f", "on_close", GetParam()}});
        start("prod", {"create:f", "write:AAAA", "sleep:1500", "write:BBBB", "close"});
        pause(400);
    }

    std::string copied() const { return slurp(root / "copy"); }
};

TEST_P(FsWaitingTest, Read) { EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AAAABBBB"); }

TEST_P(FsWaitingTest, Pread) { EXPECT_OUTPUT(run("cons", {"open:f", "pread"}), "AAAABBBB"); }

TEST_P(FsWaitingTest, Preadv) { EXPECT_OUTPUT(run("cons", {"open:f", "preadv"}), "AAAABBBB"); }

TEST_P(FsWaitingTest, Mmap) { EXPECT_OUTPUT(run("cons", {"open:f", "mmap"}), "AAAABBBB"); }

TEST_P(FsWaitingTest, SeekEnd) { EXPECT_OUTPUT(run("cons", {"open:f", "seek_end"}), "8"); }

TEST_P(FsWaitingTest, SeekData) { EXPECT_OUTPUT(run("cons", {"open:f", "seek_data:4"}), "4"); }

TEST_P(FsWaitingTest, Splice) { EXPECT_OUTPUT(run("cons", {"open:f", "splice"}), "AAAABBBB"); }

TEST_P(FsWaitingTest, CopyFileRange) {
    EXPECT_OUTPUT(run("cons", {"open:f", "copy_file_range:../copy"}), "");
    EXPECT_EQ(copied(), "AAAABBBB");
}

TEST_P(FsWaitingTest, Sendfile) {
    EXPECT_OUTPUT(run("cons", {"open:f", "sendfile:../copy"}), "");
    EXPECT_EQ(copied(), "AAAABBBB");
}

TEST_P(FsWaitingTest, Cp) {
    EXPECT_OUTPUT(run("cons", {"cp:f>../copy"}), "");
    EXPECT_EQ(copied(), "AAAABBBB");
}

TEST_P(FsWaitingTest, WriterDoesNotWait) {
    // fopen(..., "a") seeks to the end of a descriptor that is only for writing
    EXPECT_OUTPUT(finish(start("cons", {"append:f", "seek_end"}), std::chrono::milliseconds(900)),
                  "4");
}

INSTANTIATE_TEST_SUITE_P(Modes, FsWaitingTest, testing::Values("update", "no_update"),
                         [](const auto &info) { return std::string(info.param); });

TEST_F(FsStreamingTest, NoUpdateStreams) {
    startServer({{"f", "on_close", "no_update"}});
    start("prod", {"create:f", "write:AAAABBBB", "sleep:3000", "close"});
    pause(300);
    EXPECT_OUTPUT(finish(start("cons", {"open:f", "pread"}), std::chrono::milliseconds(1200)),
                  "AAAABBBB");
    EXPECT_OUTPUT(finish(start("cons", {"stat:f"}), std::chrono::milliseconds(1000)), "8");
}
