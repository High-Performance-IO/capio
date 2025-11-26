
#include "MTCL.hpp"
#include <capiocl.hpp>
#include <engine.h>
#include <gtest/gtest.h>
#include <include/utils/configuration.hpp>

capiocl::engine::Engine *capio_cl_engine;

CapioGlobalConfiguration *capio_global_configuration;

int main(int argc, char **argv) {
    capio_cl_engine            = new capiocl::engine::Engine();
    capio_global_configuration = new CapioGlobalConfiguration();
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
