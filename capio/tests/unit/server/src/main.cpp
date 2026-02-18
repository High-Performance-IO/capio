#include "common/constants.hpp"

#include <gtest/gtest.h>

char *node_name;
std::string workflow_name = CAPIO_DEFAULT_WORKFLOW_NAME;

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "utils/capiocl_adapter.hpp"
capiocl::engine::Engine capio_cl_engine(true);
const capiocl::engine::Engine &CapioCLEngine::get() { return capio_cl_engine; }

#include "capio_file.hpp"
#include "client_manager.hpp"
#include "storage_manager.hpp"