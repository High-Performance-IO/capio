#include "harness.hpp"

TEST_F(FsStreamingTest, MissingInputWaitsForItsProducer) {
    startServer({{"f"}});
    const auto reader = start("cons", {"open:f", "read"});
    pause(400);
    start("prod", {"create:f", "write:AAAA", "sleep:300", "close"});
    EXPECT_OUTPUT(finish(reader), "AAAA");
}

TEST_F(FsStreamingTest, StatOfAMissingInputWaitsForItsCommit) {
    startServer({{"f"}});
    const auto reader = start("cons", {"stat:f"});
    pause(400);
    start("prod", {"create:f", "write:AA", "sleep:500", "write:BB", "close"});
    EXPECT_OUTPUT(finish(reader), "4");
}

TEST_F(FsStreamingTest, AccessOfAMissingInputWaitsForIt) {
    startServer({{"f"}});
    const auto reader = start("cons", {"access:f", "print:done"});
    EXPECT_FALSE(finish(reader, std::chrono::milliseconds(500)).finished);
    EXPECT_OUTPUT(run("prod", {"create:f", "close"}), "");
    EXPECT_OUTPUT(finish(reader), "done");
}

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

TEST_F(FsStreamingTest, GlobsDeclareTheirPaths) {
    startServer({{"d/*"}});
    const auto reader = start("cons", {"open:d/part3", "read"});
    pause(300);
    EXPECT_OUTPUT(run("prod", {"mkdir:d", "create:d/part3", "write:A", "close"}), "");
    EXPECT_OUTPUT(finish(reader), "A");
}

TEST_F(FsStreamingTest, WithoutAConfigEveryMissingPathWaits) {
    startServer({});
    const auto reader = start("reader", {"open:f", "read"});
    pause(400);
    EXPECT_OUTPUT(run("writer", {"create:f", "write:AAAA"}), "");
    EXPECT_OUTPUT(finish(reader), "AAAA");
}

TEST_F(FsStreamingTest, WithoutAConfigStatWaitsForTheCommit) {
    // Committed when the undeclared producer ends, not when it creates the file
    startServer({});
    const auto stat   = start("reader", {"stat:f"});
    const auto access = start("prober", {"access:f", "stat:f"});
    pause(400);
    EXPECT_OUTPUT(run("writer", {"create:f", "sleep:600", "write:AAAA"}), "");
    EXPECT_OUTPUT(finish(stat), "4");
    EXPECT_OUTPUT(finish(access), "4");
}

TEST_F(FsStreamingTest, PathsAreNormalized) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"create:f", "write:A", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"open:" + work.string() + "/./f", "read"}), "A");
}
