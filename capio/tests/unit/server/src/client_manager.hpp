#ifndef CAPIO_CLIENT_MANAGER_HPP
#define CAPIO_CLIENT_MANAGER_HPP

#include "client-manager/client_manager.hpp"

#include <thread>

TEST(clientManagerTestSuite, testReplyToNonClient) {
    ClientManager client_manager;
    char buffer[1024];
    EXPECT_THROW(client_manager.replyToClient(-1, 0, buffer, 0), std::runtime_error);
}

TEST(clientManagerTestSuite, testGetNumberOfConnectedClients) {
    ClientManager client_manager;
    EXPECT_EQ(client_manager.getConnectedPosixClients(), 0);

    client_manager.registerClient(1234);

    EXPECT_EQ(client_manager.getConnectedPosixClients(), 1);

    client_manager.removeClient(1234);

    EXPECT_EQ(client_manager.getConnectedPosixClients(), 0);
}

TEST(ClientManagerTestSuite, testFailedRequestCode) {

    ClientManager client_manager;

    // NOTE: there is no need to delete this object as it is only attaching to the shm allocated by
    // client_manager. Also calling delete on this raises std::terminate as an exception is thrown
    // in the destructor
    // TODO: change behaviour of ERR_EXIT to not throw exceptions but only print error and continue
    const auto request_queue =
        new CircularBuffer<char>(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE);

    char req[CAPIO_REQ_MAX_SIZE], new_req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "123 aaaa bbbb cccc");
    request_queue->write(req, CAPIO_REQ_MAX_SIZE);

    EXPECT_EQ(client_manager.readNextRequest(new_req), 123);

    sprintf(req, "abc aaaa bbbb cccc");
    request_queue->write(req, CAPIO_REQ_MAX_SIZE);

    EXPECT_EQ(client_manager.readNextRequest(new_req), -1);
}

#endif // CAPIO_CLIENT_MANAGER_HPP
