#include "harness.hpp"

/*
 * Every call that hands out data of a committed file gets all of it, whatever the fire rule.
 */
class FsWaitingTest : public FsStreamingTest {
  protected:
    std::string copied() const { return slurp(root / "copy"); }

    /// @brief Run @p check on f, committed with "AAAABBBB", in update mode and in no_update mode
    template <typename Check> void inBothModes(const Check &check) {
        for (const char *mode : {"update", "no_update"}) {
            SCOPED_TRACE(mode);
            stopServer();
            // A fresh directory: CAPIO-CL's token files would tell the next server f is committed
            std::filesystem::remove_all(work);
            std::filesystem::create_directory(work);
            std::filesystem::remove(root / "copy");
            startServer({{"f", "on_close", mode}});
            EXPECT_OUTPUT(run("prod", {"create:f", "write:AAAABBBB", "close"}), "");
            check();
        }
    }
};

TEST_F(FsWaitingTest, Read) {
    inBothModes([this] { EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AAAABBBB"); });
}

TEST_F(FsWaitingTest, Pread) {
    inBothModes([this] { EXPECT_OUTPUT(run("cons", {"open:f", "pread"}), "AAAABBBB"); });
}

TEST_F(FsWaitingTest, Preadv) {
    inBothModes([this] { EXPECT_OUTPUT(run("cons", {"open:f", "preadv"}), "AAAABBBB"); });
}

TEST_F(FsWaitingTest, SeekEnd) {
    inBothModes([this] { EXPECT_OUTPUT(run("cons", {"open:f", "seek_end"}), "8"); });
}

TEST_F(FsWaitingTest, SeekData) {
    inBothModes([this] { EXPECT_OUTPUT(run("cons", {"open:f", "seek_data:4"}), "4"); });
}

TEST_F(FsWaitingTest, Splice) {
    inBothModes([this] { EXPECT_OUTPUT(run("cons", {"open:f", "splice"}), "AAAABBBB"); });
}

TEST_F(FsWaitingTest, CopyFileRange) {
    inBothModes([this] {
        EXPECT_OUTPUT(run("cons", {"open:f", "copy_file_range:../copy"}), "");
        EXPECT_EQ(copied(), "AAAABBBB");
    });
}

TEST_F(FsWaitingTest, Sendfile) {
    inBothModes([this] {
        EXPECT_OUTPUT(run("cons", {"open:f", "sendfile:../copy"}), "");
        EXPECT_EQ(copied(), "AAAABBBB");
    });
}

TEST_F(FsWaitingTest, Cp) {
    inBothModes([this] {
        EXPECT_OUTPUT(run("cons", {"cp:f>../copy"}), "");
        EXPECT_EQ(copied(), "AAAABBBB");
    });
}

TEST_F(FsWaitingTest, WriterSeeksToItsEnd) {
    // fopen(..., "a") seeks to the end of a descriptor that is only for writing
    inBothModes([this] { EXPECT_OUTPUT(run("cons", {"append:f", "seek_end"}), "8"); });
}
