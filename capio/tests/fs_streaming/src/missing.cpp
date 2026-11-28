#include "harness.hpp"

TEST_F(FsStreamingTest, MissingUndeclaredPathFailsEachTime) {
    startServer({{"f"}});
    // Again after the open: a lookup must not turn the path into a declared one
    EXPECT_OUTPUT(run("cons", {"stat:cfg", "open:cfg", "stat:cfg", "access:cfg"}),
                  "stat=ENOENTopen=ENOENTstat=ENOENTaccess=ENOENT");
}

TEST_F(FsStreamingTest, MissingInputOfAnotherAppFails) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("bystander", {"open:f"}), "open=ENOENT");
}

TEST_F(FsStreamingTest, ProducerDoesNotWaitForItsOutput) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"open:f", "stat:f"}), "open=ENOENTstat=ENOENT");
}

TEST_F(FsStreamingTest, PathsAreNormalized) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"create:f", "write:A", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"open:" + work.string() + "/./f", "read"}), "A");
}
