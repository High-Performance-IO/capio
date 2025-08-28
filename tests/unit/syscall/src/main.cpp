#include "capio/constants.hpp"
#include <gtest/gtest.h>

std::string workflow_name = CAPIO_DEFAULT_WORKFLOW_NAME;

int main(int argc, char **argv, char **envp) {
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}