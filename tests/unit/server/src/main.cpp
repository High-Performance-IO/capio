#include <gtest/gtest.h>
#include <thread>
std::string node_name;

#include "CapioFile.hpp"
#include "TestCommService.hpp"

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
