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
};

TEST_P(FsWaitingTest, Read) { EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AAAABBBB"); }

TEST_P(FsWaitingTest, WriterDoesNotWait) {
    // fopen(..., "a") seeks to the end of a descriptor that is only for writing
    EXPECT_OUTPUT(finish(start("cons", {"append:f", "seek_end"}), std::chrono::milliseconds(900)),
                  "4");
}

INSTANTIATE_TEST_SUITE_P(Modes, FsWaitingTest, testing::Values("update", "no_update"),
                         [](const auto &info) { return std::string(info.param); });
