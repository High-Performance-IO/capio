#include <gtest/gtest.h>

#include "capiocl.hpp"
#include "capiocl/configuration.h"
#include "capiocl/engine.h"
#include "common/constants.hpp"
#include "fs-streaming/fs_streaming.hpp"

extern capiocl::engine::Engine *capio_cl_engine;

class FsStreamingStateTest : public testing::Test {
  protected:
    static constexpr pid_t PRODUCER = 4242;

    capiocl::engine::Engine *previous_engine = nullptr;
    FsStreaming state;

    void SetUp() override {
        previous_engine = capio_cl_engine;
        capio_cl_engine = new capiocl::engine::Engine(capiocl::configuration::CapioClConfiguration(
            {{"capiocl.monitor.mcast.enabled", "true"}, {"capiocl.monitor.mcast.delay_ms", "1"}}));
        state.addThread(PRODUCER, PRODUCER, "producer");
    }

    void TearDown() override {
        delete capio_cl_engine;
        capio_cl_engine = previous_engine;
    }

    void create(const std::string &path, const std::string &rule) {
        capio_cl_engine->newFile(path);
        capio_cl_engine->setCommitRule(path, rule);
        state.addProducer(PRODUCER, path);
        state.addOpener(PRODUCER, path);
    }
};

TEST_F(FsStreamingStateTest, SuccessfulCloseCommits) {
    create("/fs_state/ok", capiocl::commitRules::ON_CLOSE);
    state.closed(PRODUCER, "/fs_state/ok", CloseKind::EXPLICIT, true);
    EXPECT_TRUE(capio_cl_engine->isCommitted("/fs_state/ok"));
}

TEST_F(FsStreamingStateTest, FailedCloseNeverCommits) {
    const std::string path = "/fs_state/failed";
    create(path, capiocl::commitRules::ON_CLOSE);
    state.closeFailed(PRODUCER, path, ENOSPC);
    state.closed(PRODUCER, path, CloseKind::EXPLICIT, true); // a later close that succeeds
    state.commit(path);                                      // and the end of the producer
    EXPECT_FALSE(capio_cl_engine->isCommitted(path));
}

TEST_F(FsStreamingStateTest, ImplicitCloseCountsOnlyInTheOpener) {
    const std::string path = "/fs_state/inherited";
    create(path, capiocl::commitRules::ON_CLOSE);
    constexpr pid_t CHILD = 4243;
    state.addThread(CHILD, CHILD, "producer");
    state.closed(CHILD, path, CloseKind::IMPLICIT, true);
    EXPECT_FALSE(capio_cl_engine->isCommitted(path));
    state.closed(PRODUCER, path, CloseKind::IMPLICIT, true);
    EXPECT_TRUE(capio_cl_engine->isCommitted(path));
}

TEST_F(FsStreamingStateTest, AppsWithoutANameAreOnTheirOwn) {
    state.addThread(10, 10, CAPIO_DEFAULT_APP_NAME);
    state.addThread(11, 10, CAPIO_DEFAULT_APP_NAME);
    state.addThread(20, 20, CAPIO_DEFAULT_APP_NAME);
    state.addProducer(10, "/fs_state/unnamed");
    EXPECT_TRUE(state.isProducer(11, "/fs_state/unnamed"));
    EXPECT_FALSE(state.isProducer(20, "/fs_state/unnamed"));
}
