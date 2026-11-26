#include "harness.hpp"

/*
 * Every call that hands out data of a committed file gets all of it, whatever the fire rule.
 */
class FsWaitingTest : public FsStreamingTest, public testing::WithParamInterface<const char *> {
  protected:
    void SetUp() override {
        FsStreamingTest::SetUp();
        startServer({{"f", "on_close", GetParam()}});
        EXPECT_OUTPUT(run("prod", {"create:f", "write:AAAABBBB", "close"}), "");
    }

    std::string copied() const { return slurp(root / "copy"); }
};

TEST_P(FsWaitingTest, Read) { EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AAAABBBB"); }

TEST_P(FsWaitingTest, Pread) { EXPECT_OUTPUT(run("cons", {"open:f", "pread"}), "AAAABBBB"); }

TEST_P(FsWaitingTest, Preadv) { EXPECT_OUTPUT(run("cons", {"open:f", "preadv"}), "AAAABBBB"); }

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

TEST_P(FsWaitingTest, WriterSeeksToItsEnd) {
    // fopen(..., "a") seeks to the end of a descriptor that is only for writing
    EXPECT_OUTPUT(run("cons", {"append:f", "seek_end"}), "8");
}

INSTANTIATE_TEST_SUITE_P(Modes, FsWaitingTest, testing::Values("update", "no_update"),
                         [](const auto &info) { return std::string(info.param); });
