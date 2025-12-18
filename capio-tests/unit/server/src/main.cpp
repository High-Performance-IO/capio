#include <capio/constants.hpp>
#include <capiocl.hpp>
#include <capiocl.hpp>
#include "capiocl/engine.h"
#include <gtest/gtest.h>
#include <include/utils/configuration.hpp>
#define syscall_no_intercept syscall

CapioGlobalConfiguration* capio_global_configuration;

std::string workflow_name = CAPIO_DEFAULT_WORKFLOW_NAME;

std::string node_name;

capiocl::engine::Engine *capio_cl_engine;

#include "CapioCacheSPSCQueueTests.hpp"
#include "CapioFileTests.hpp"


int main(int argc, char **argv) {
    capio_global_configuration = new CapioGlobalConfiguration();
    capio_cl_engine = new capiocl::engine::Engine();
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
