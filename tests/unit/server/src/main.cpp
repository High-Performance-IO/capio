#include <gtest/gtest.h>
#include <thread>
std::string node_name;

// TODO: restore tests on CapioFile.hpp
// #include "CapioFile.hpp"
#include "TestCommService.hpp"

int main(int argc, char **argv) {
    node_name.reserve(HOST_NAME_MAX);
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
