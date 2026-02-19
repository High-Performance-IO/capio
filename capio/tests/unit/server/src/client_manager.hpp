#ifndef CAPIO_CLIENT_MANAGER_HPP
#define CAPIO_CLIENT_MANAGER_HPP

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

    // NOTE: there is no need to delete this object as it is only attaching to the shm allocated by
    // client_manager. Also calling delete on this raises std::terminate as an exception is thrown
    // in the destructor
    // TODO: change behaviour of ERR_EXIT to not throw exceptions but only print error and continue
    const auto request_queue =
        new CircularBuffer<char>(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE);

    char req[CAPIO_REQ_MAX_SIZE], new_req[CAPIO_REQ_MAX_SIZE];
    constexpr int TEST_REQ_CODE = 123;
    sprintf(req, "%04d aaaa bbbb cccc", TEST_REQ_CODE);
    request_queue->write(req, CAPIO_REQ_MAX_SIZE);
    const auto return_code = client_manager->readNextRequest(new_req);
    std::cout << new_req << std::endl;
    EXPECT_EQ(return_code, TEST_REQ_CODE);

    sprintf(req, "abc aaaa bbbb cccc");
    request_queue->write(req, CAPIO_REQ_MAX_SIZE);

    EXPECT_EQ(client_manager->readNextRequest(new_req), -1);
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

#endif // CAPIO_CLIENT_MANAGER_HPP
