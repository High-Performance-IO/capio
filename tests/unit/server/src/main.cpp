#include <capio/constants.hpp>
#include <gtest/gtest.h>

#define syscall_no_intercept syscall

std::string workflow_name = CAPIO_DEFAULT_WORKFLOW_NAME;

std::string node_name;

#include "CapioCacheSPSCQueueTests.hpp"
#include "CapioFileTests.hpp"

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
