#include <gtest/gtest.h>

/*
 * The tests run preloaded with a CAPIO client library, against a CAPIO server that must already be
 * running for CAPIO_DIR: every client waits for the answer to its handshake, which happens before
 * main, so the tests cannot start the server themselves.
 */
int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
