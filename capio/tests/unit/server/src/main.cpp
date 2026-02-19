#include <gtest/gtest.h>

char *node_name;

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "common/constants.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/location.hpp"

capiocl::engine::Engine *capio_cl_engine;
const capiocl::engine::Engine &CapioCLEngine::get() { return *capio_cl_engine; }

class CapioServerUnitTestEnviron : public testing::Test {
  protected:
    void SetUp() override {
        capio_cl_engine = new capiocl::engine::Engine(true);
        node_name       = new char[HOST_NAME_MAX];
        gethostname(node_name, HOST_NAME_MAX);
        open_files_location();
    }

    void TearDown() override { delete capio_cl_engine; }
};

class ClientManagerTestEnvironment : public CapioServerUnitTestEnviron {
  protected:
    ClientManager *client_manager = nullptr;
    void SetUp() override {
        CapioServerUnitTestEnviron::SetUp();
        client_manager = new ClientManager();
    }

    void TearDown() override {
        delete client_manager;
        CapioServerUnitTestEnviron::TearDown();
    }
};

class StorageManagerTestEnvironment : public ClientManagerTestEnvironment {
  protected:
    StorageManager *storage_manager = nullptr;
    void SetUp() override {
        ClientManagerTestEnvironment::SetUp();
        storage_manager = new StorageManager(client_manager);
    }

    void TearDown() override {
        delete storage_manager;
        ClientManagerTestEnvironment::TearDown();
    }
};

/// Include test sources

#include "capio_file.hpp"
#include "client_manager.hpp"
#include "storage_manager.hpp"