#include "harness.hpp"

/*
 * Who writes a file produces it, so that on_termination commits it when the writer ends: also
 * without O_CREAT.
 */
class FsProducerTest : public FsStreamingTest {
  protected:
    void SetUp() override {
        FsStreamingTest::SetUp();
        startServer({{"f", "on_termination"}});
    }

    Result consume() { return finish(start("cons", {"open:f", "read"})); }
};

TEST_F(FsProducerTest, RewriteWithoutCreateCommits) {
    std::ofstream(work / "f") << "old";
    EXPECT_OUTPUT(run("prod", {"rewrite:f", "write:A", "close"}), "");
    EXPECT_OUTPUT(consume(), "A");
}
