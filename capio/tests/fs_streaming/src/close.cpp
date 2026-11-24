#include "harness.hpp"

/*
 * on_close counts the exit of the producer as its close.
 */
TEST_F(FsStreamingTest, ExitCounts) {
    startServer({{"f"}});
    EXPECT_OUTPUT(run("prod", {"create:f", "write:A"}), "");
    EXPECT_OUTPUT(run("cons", {"open:f", "read"}), "A");
}
