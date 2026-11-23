#include <gtest/gtest.h>

#include <cstdlib>

#include "remote/backend/mtcl.hpp"

/*
 * Peers connect to the address in the token a server advertises. When CAPIO_MTCL_ADVERTISE_HOST
 * names one (e.g. the InfiniBand interface), the token must carry it, followed by the hostname
 * that still identifies the node.
 */
TEST(MTCLBackendTest, advertisesTheAddressSetInCapioMtclAdvertiseHost) {
    setenv("CAPIO_MTCL_ADVERTISE_HOST", "10.1.2.3", 1);
    EXPECT_EQ(make_advertisement_token("TCP", "1234", "node-a"), "TCP:10.1.2.3:1234|node-a");

    unsetenv("CAPIO_MTCL_ADVERTISE_HOST");
    EXPECT_EQ(make_advertisement_token("TCP", "1234", "node-a"), "TCP:node-a:1234");
}
