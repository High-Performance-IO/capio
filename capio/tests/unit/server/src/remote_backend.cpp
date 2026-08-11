#include <gtest/gtest.h>

#include "common/requests.hpp"
#include "remote/backend.hpp"

TEST(RemoteRequestTest, ParsesOwnedRequest) {
    RemoteRequest request(std::string("0002 payload"), "node-a");

    EXPECT_EQ(request.get_code(), CAPIO_SERVER_REQUEST_STAT);
    EXPECT_STREQ(request.get_content(), "payload");
    EXPECT_EQ(request.get_source(), "node-a");
}

TEST(RemoteRequestTest, RejectsMalformedRequest) {
    EXPECT_EQ(RemoteRequest(std::string(), "node-a").get_code(), -1);
    EXPECT_EQ(RemoteRequest(std::string("0002payload"), "node-a").get_code(), -1);
    EXPECT_EQ(RemoteRequest(std::string("xxxx payload"), "node-a").get_code(), -1);
}
