#include <gtest/gtest.h>
#include <thread>

std::string node_name;
#include "MTCL.hpp"
#include <capio/logger.hpp>

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
