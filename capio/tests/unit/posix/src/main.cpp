#include <gtest/gtest.h>

#include "broadcast_flush.hpp"
#include "realpath.hpp"
#include "write_read_cache.hpp"

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}