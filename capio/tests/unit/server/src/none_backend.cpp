#include <gtest/gtest.h>

#include "remote/backend/none.hpp"

TEST(NoneBackendTest, OperationsAreSafeNoOps) {
    char program[] = "capio_server";
    char *argv[]   = {program, nullptr};
    NoneBackend backend(1, argv);
    char data[] = "data";

    const auto request = backend.read_next_request();
    EXPECT_EQ(request.get_code(), -1);
    EXPECT_TRUE(request.get_source().empty());
    EXPECT_STREQ(request.get_content(), "");
    EXPECT_NO_THROW(backend.handshake_servers());
    EXPECT_NO_THROW(backend.send_request("request", 7, "node-a"));
    EXPECT_NO_THROW(backend.send_file("request", 7, data, 4, "node-a"));
    EXPECT_NO_THROW(backend.recv_file(data, 4, "node-a"));
    EXPECT_NO_THROW(backend.connect_to("node-a"));
}
