#ifndef CAPIO_CLIENT_MANAGER_HPP
#define CAPIO_CLIENT_MANAGER_HPP
#include <gtest/gtest.h>

#include <memory>

#include <client-manager/client_manager.hpp>
#include <utils/capiocl_adapter.hpp>

extern ClientManager *client_manager;

TEST(ClientManagerTestEnvironment, testReplyToNonClient) {
    char buffer[1024];
    EXPECT_THROW(client_manager->replyToClient(-1, 0, buffer, 0), std::runtime_error);
}

TEST(ClientManagerTestEnvironment, testGetNumberOfConnectedClients) {

    EXPECT_EQ(client_manager->getConnectedPosixClients(), 0);

    client_manager->registerClient(1234);
    EXPECT_EQ(client_manager->getConnectedPosixClients(), 1);

    client_manager->removeClient(1234);
    EXPECT_EQ(client_manager->getConnectedPosixClients(), 0);
}

TEST(ClientManagerTestEnvironment, testFailedRequestCode) {
    // Attached to the request queue of client_manager, as a client is. Its destruction unlinks the
    // names of the queue, so that the teardown of client_manager finds them gone: that is not an
    // error, and the memory client_manager has mapped stays usable.
    auto request_queue = std::make_unique<CircularBuffer<char>>(
        SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE,
        CapioCLEngine::get().getWorkflowName());

    char req[CAPIO_REQ_MAX_SIZE], new_req[CAPIO_REQ_MAX_SIZE];
    constexpr int TEST_REQ_CODE = 123;
    sprintf(req, "%04d aaaa bbbb cccc", TEST_REQ_CODE);
    request_queue->write(req, CAPIO_REQ_MAX_SIZE);
    const auto return_code = client_manager->readNextRequest(new_req);
    EXPECT_EQ(return_code, TEST_REQ_CODE);
    EXPECT_STREQ(new_req, "aaaa bbbb cccc");

    sprintf(req, "abc aaaa bbbb cccc");
    request_queue->write(req, CAPIO_REQ_MAX_SIZE);

    EXPECT_EQ(client_manager->readNextRequest(new_req), -1);
    request_queue.reset();
}

TEST(ClientManagerTestEnvironment, testAddAndRemoveProducedFiles) {

    client_manager->registerClient(1234, "test_app");
    client_manager->registerProducedFile(1234, "test.txt");

    EXPECT_TRUE(client_manager->isProducer(1234, "test.txt"));

    client_manager->removeProducedFile(1234, "test.txt");
    EXPECT_FALSE(client_manager->isProducer(1234, "test.txt"));

    client_manager->registerProducedFile(1111, "test1.txt");
    EXPECT_FALSE(client_manager->isProducer(1111, "test1.txt"));
}

TEST(ClientManagerTestEnvironment, registerProducedFileIsIdempotentPerPath) {
    const pid_t tid = 2222;
    client_manager->registerClient(tid, "test_app");

    constexpr int NTIMES = 10000;
    for (int i = 0; i < NTIMES; ++i) {
        client_manager->registerProducedFile(tid, "p1_0.dat");
    }

    // a file written 10000 times must be registered exactly once
    EXPECT_EQ(client_manager->getProducedFiles(tid).size(), 1u);

    // distinct files are all still registered
    client_manager->registerProducedFile(tid, "mid_0_0.dat");
    EXPECT_EQ(client_manager->getProducedFiles(tid).size(), 2u);
    EXPECT_TRUE(client_manager->isProducer(tid, "p1_0.dat"));
    EXPECT_TRUE(client_manager->isProducer(tid, "mid_0_0.dat"));

    client_manager->removeClient(tid);
}

TEST(ClientManagerTestEnvironment, threadKnowsItsProcess) {
    client_manager->registerClient(1240, "app", false, 1239);
    client_manager->registerClient(1241, "app");
    EXPECT_EQ(client_manager->getPid(1240), 1239);
    EXPECT_EQ(client_manager->getPid(1241), 1241);

    client_manager->removeClient(1240);
    client_manager->removeClient(1241);
    EXPECT_EQ(client_manager->getPid(1240), 1240);
}

#endif // CAPIO_CLIENT_MANAGER_HPP
