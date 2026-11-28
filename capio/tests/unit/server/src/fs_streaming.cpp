#include <gtest/gtest.h>

#include <memory>

#include "capiocl.hpp"
#include "capiocl/configuration.h"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "common/constants.hpp"
#include "storage/fs_storage_manager.hpp"

extern capiocl::engine::Engine *capio_cl_engine;
extern ClientManager *client_manager;

/// Closes and producers of the file system variant, on the server's side
class FsStreamingStateTest : public testing::Test {
  protected:
    static constexpr pid_t PRODUCER = 4242;

    capiocl::engine::Engine *previous_engine = nullptr;
    std::vector<pid_t> clients;
    std::unique_ptr<FsStorageManager> manager;

    void SetUp() override {
        previous_engine = capio_cl_engine;
        capio_cl_engine = new capiocl::engine::Engine(capiocl::configuration::CapioClConfiguration(
            {{"capiocl.monitor.mcast.enabled", "true"}, {"capiocl.monitor.mcast.delay_ms", "1"}}));
        addClient(PRODUCER, "producer");
        manager = std::make_unique<FsStorageManager>();
    }

    void TearDown() override {
        for (const auto tid : clients) {
            client_manager->removeClient(tid);
        }
        delete capio_cl_engine;
        capio_cl_engine = previous_engine;
    }

    void addClient(const pid_t tid, const std::string &app) {
        client_manager->registerClient(tid, app);
        clients.push_back(tid);
    }

    void create(const std::string &path, const std::string &rule) {
        capio_cl_engine->newFile(path);
        capio_cl_engine->setCommitRule(path, rule);
        client_manager->registerProducedFile(PRODUCER, path);
    }
};

TEST_F(FsStreamingStateTest, SuccessfulCloseCommits) {
    create("/fs_state/ok", capiocl::commitRules::ON_CLOSE);
    manager->closed(PRODUCER, "/fs_state/ok");
    EXPECT_TRUE(capio_cl_engine->isCommitted("/fs_state/ok"));
}

TEST_F(FsStreamingStateTest, FailedCloseNeverCommits) {
    const std::string path = "/fs_state/failed";
    create(path, capiocl::commitRules::ON_CLOSE);
    manager->closeFailed(PRODUCER, path, ENOSPC);
    manager->closed(PRODUCER, path); // a later close that succeeds
    manager->commit(path);           // and the end of the producer
    EXPECT_FALSE(capio_cl_engine->isCommitted(path));
}

TEST_F(FsStreamingStateTest, AnAppProducesUntilItsLastClientExits) {
    addClient(30, "app");
    addClient(31, "app");
    client_manager->registerProducedFile(30, "/fs_state/app_output");
    client_manager->removeClient(30);
    EXPECT_TRUE(client_manager->isProducer(31, "/fs_state/app_output"));
}

TEST_F(FsStreamingStateTest, CloseOfADescriptorClosesItsFile) {
    const std::string path = "/fs_state/by_descriptor";
    create(path, capiocl::commitRules::ON_CLOSE);
    manager->addFileToPid(PRODUCER, 3, path);
    manager->closed(PRODUCER, PRODUCER, 3, 0);
    EXPECT_TRUE(capio_cl_engine->isCommitted(path));
    EXPECT_EQ(manager->getPath(PRODUCER, 3), "");
}

TEST_F(FsStreamingStateTest, AnotherThreadClosesADescriptorOfItsProcess) {
    constexpr pid_t THREAD = PRODUCER + 1;
    const std::string path = "/fs_state/other_thread";
    addClient(THREAD, "producer");
    create(path, capiocl::commitRules::ON_CLOSE);
    manager->addFileToPid(PRODUCER, 3, path);
    manager->closed(THREAD, PRODUCER, 3, 0);
    EXPECT_TRUE(capio_cl_engine->isCommitted(path));
}

TEST_F(FsStreamingStateTest, CopyOfADescriptorOutlivesTheOriginal) {
    const std::string path = "/fs_state/copy";
    manager->addFileToPid(PRODUCER, 3, path);
    manager->dup(PRODUCER, 3, 4);
    manager->closed(PRODUCER, PRODUCER, 3, 0);
    EXPECT_EQ(manager->getPath(PRODUCER, 4), path);
}

TEST_F(FsStreamingStateTest, ReusedNumberNamesTheNewFile) {
    manager->addFileToPid(PRODUCER, 3, "/fs_state/first");
    manager->closed(PRODUCER, PRODUCER, 3, 0);
    manager->addFileToPid(PRODUCER, 3, "/fs_state/second");
    EXPECT_EQ(manager->getPath(PRODUCER, 3), "/fs_state/second");
}

TEST_F(FsStreamingStateTest, ProcessesHaveTheirOwnDescriptors) {
    manager->addFileToPid(PRODUCER, 3, "/fs_state/parent");
    manager->addFileToPid(PRODUCER + 1, 3, "/fs_state/child");
    EXPECT_EQ(manager->getPath(PRODUCER, 3), "/fs_state/parent");
    EXPECT_EQ(manager->getPath(PRODUCER + 1, 3), "/fs_state/child");
}

TEST_F(FsStreamingStateTest, FailedCloseOfADescriptorNeverCommits) {
    const std::string path = "/fs_state/failed_descriptor";
    create(path, capiocl::commitRules::ON_CLOSE);
    manager->addFileToPid(PRODUCER, 3, path);
    manager->closed(PRODUCER, PRODUCER, 3, EIO);
    manager->commit(path);
    EXPECT_FALSE(capio_cl_engine->isCommitted(path));
}

TEST_F(FsStreamingStateTest, CloseOfAnUnknownDescriptorDoesNothing) {
    const std::string path = "/fs_state/unknown";
    create(path, capiocl::commitRules::ON_CLOSE);
    manager->closed(PRODUCER, PRODUCER, 9, 0);
    EXPECT_FALSE(capio_cl_engine->isCommitted(path));
}
