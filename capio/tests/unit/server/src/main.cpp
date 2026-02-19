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
StorageManager *storage_manager = nullptr;
ClientManager *client_manager   = nullptr;

const capiocl::engine::Engine &CapioCLEngine::get() { return *capio_cl_engine; }

class ServerUnitTestEnvironment : public testing::Environment {
  public:
    explicit ServerUnitTestEnvironment() = default;

    void SetUp() override {
        capio_cl_engine = new capiocl::engine::Engine(false);
        node_name       = new char[HOST_NAME_MAX];
        gethostname(node_name, HOST_NAME_MAX);
        open_files_location();

        client_manager  = new ClientManager();
        storage_manager = new StorageManager();
    }

    void TearDown() override {
        delete storage_manager;
        delete client_manager;
        delete capio_cl_engine;
    }
};

/// Include test sources

#include "capio_file.hpp"
#include "client_manager.hpp"
#include "storage_manager.hpp"

int main(int argc, char **argv, char **envp) {
    testing::InitGoogleTest(&argc, argv);

    testing::AddGlobalTestEnvironment(new ServerUnitTestEnvironment());
    return RUN_ALL_TESTS();
}