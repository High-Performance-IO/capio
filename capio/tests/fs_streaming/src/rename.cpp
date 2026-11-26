#include "harness.hpp"

TEST_F(FsStreamingTest, TemporaryNameThenRenameCommits) {
    startServer({{"f", "on_termination"}});
    EXPECT_OUTPUT(run("prod", {"create:f.tmp", "write:AAAA", "close", "rename:f.tmp>f"}), "");
    EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AAAA");
}

TEST_F(FsStreamingTest, RenameCarriesTheCountOfCloses) {
    startServer({{"f", "on_close:2"}});
    EXPECT_OUTPUT(run("prod", {"create:f.tmp", "write:A", "close", "append:f.tmp", "write:B",
                               "close", "rename:f.tmp>f"}),
                  "");
    EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AB");
}

TEST_F(FsStreamingTest, RenameByAnotherProcessKeepsTheHistory) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"create:f.tmp", "write:A", "close"}), "");
    EXPECT_OUTPUT(run("prod", {"rename:f.tmp>f"}), "");
    EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "A");
}

TEST_F(FsStreamingTest, RenameOfADirectoryCarriesItsFiles) {
    startServer({{"out/f"}});
    EXPECT_OUTPUT(run("prod", {"mkdir:out.tmp", "create:out.tmp/f", "write:A", "close",
                               "rename:out.tmp>out"}),
                  "");
    EXPECT_OUTPUT(run("cons", {"open:out/f", "read"}), "A");
}

TEST_F(FsStreamingTest, MoveFromOutsideIsOneClose) {
    startServer({{"f"}});
    std::ofstream(root / "outside") << "AAAA";
    EXPECT_OUTPUT(run("prod", {"rename:../outside>f"}), "");
    EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "AAAA");
}

TEST_F(FsStreamingTest, FailedRenameChangesNothing) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"rename:missing>f"}), "rename=ENOENT");
}

// Both names go in one request, which is longer than one: it is sent in pieces
TEST_F(FsStreamingTest, RenameBetweenLongNamesCommits) {
    const std::string name(200, 'x');
    startServer({{name, "on_termination"}});
    EXPECT_OUTPUT(run("prod", {"create:" + name + ".tmp", "write:AAAA", "close",
                               "rename:" + name + ".tmp>" + name}),
                  "");
    EXPECT_OUTPUT(run("cons", {"open:" + name, "read"}), "AAAA");
}
