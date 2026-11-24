#include "harness.hpp"

TEST_F(FsStreamingTest, FailedWriteReportsItsError) {
    startServer({{"unused"}});
    std::ofstream(work / "f") << "data";
    EXPECT_OUTPUT(run("prod", {"create:unused", "close", "open:unused", "write:x"}), "write=EBADF");
}

TEST_F(FsStreamingTest, FailedSeekReportsItsError) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"create:f", "seek:-5", "close"}), "seek=EINVAL");
}

TEST_F(FsStreamingTest, FailedExclusiveCreateReportsItsError) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"create:f", "close", "excl:f"}), "excl=EEXIST");
}

TEST_F(FsStreamingTest, ThreadDoesNotHang) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"create:f", "thread:write:AAAA", "close", "print:done"}), "done");
}

TEST_F(FsStreamingTest, PathWithSpaces) {
    startServer({{"a b"}, {"a"}});
    EXPECT_OUTPUT(run("prod", {"create:a b", "write:AAAA", "close"}), "");
    EXPECT_OUTPUT(run("cons", {"open:a b", "read"}), "AAAA");
}

TEST_F(FsStreamingTest, CapioDirNeverWaits) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("cons", {"stat:" + work.string()}), "dir");
}

TEST_F(FsStreamingTest, PlainDirectoryNeverWaits) {
    startServer({{"f"}});
    std::filesystem::create_directory(work / "plain");
    // Twice: the first lookup is enough to make contains() true
    EXPECT_OUTPUT(run("cons", {"stat:plain", "open:plain", "stat:plain"}), "dirdir");
}
