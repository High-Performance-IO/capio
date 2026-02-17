#ifndef CAPIO_CLIENT_MANAGER_HPP
#define CAPIO_CLIENT_MANAGER_HPP

char *node_name;
std::string workflow_name;

#include "capiocl.hpp"
#include "utils/capiocl_adapter.hpp"
capiocl::Engine capio_cl_engine;
const capiocl::Engine &CapioCLEngine::get() { return capio_cl_engine; }

#include "client-manager/client_manager.hpp"
ClientManager *clientManager;

TEST(clientManagerTestSuite, testReplyToNonClient) {
    clientManager = new ClientManager();
    char buffer[1024];
    EXPECT_THROW(clientManager->replyToClient(-1, 0, buffer, 0), std::runtime_error);
}

#endif // CAPIO_CLIENT_MANAGER_HPP
