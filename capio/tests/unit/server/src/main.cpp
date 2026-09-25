#include <gtest/gtest.h>

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "common/constants.hpp"
#include "remote/discovery.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/location.hpp"
#include "utils/runtime_configuration.hpp"

capiocl::engine::Engine *capio_cl_engine = nullptr;
StorageManager *storage_manager          = nullptr;
ClientManager *client_manager            = nullptr;
Backend *backend                         = nullptr;
DiscoveryService *discovery_service      = nullptr;

class TestDiscovery final : public DiscoveryInterface {
  public:
    void start(const std::string &, unsigned int) override {}
    void stop() override {}
};

const capiocl::engine::Engine &CapioCLEngine::get() { return *capio_cl_engine; }

class ServerUnitTestEnvironment : public testing::Environment {
  public:
    explicit ServerUnitTestEnvironment() = default;

    void SetUp() override {
        configure_server_runtime("/tmp", CAPIO_CACHE_LINES_DEFAULT, CAPIO_CACHE_LINE_SIZE_DEFAULT,
                                 CAPIO_DEFAULT_FILE_INITIAL_SIZE, 0);
        capio_cl_engine = new capiocl::engine::Engine(false);
        capio_cl_engine->setWorkflowName(CAPIO_DEFAULT_WORKFLOW_NAME);
        client_manager    = new ClientManager();
        storage_manager   = new StorageManager();
        discovery_service = new DiscoveryService(std::make_unique<TestDiscovery>());
    }

    void TearDown() override {
        delete storage_manager;
        delete client_manager;
        delete capio_cl_engine;
        delete discovery_service;
    }
};

int main(int argc, char **argv, char **envp) {
    testing::InitGoogleTest(&argc, argv);

    testing::AddGlobalTestEnvironment(new ServerUnitTestEnvironment());
    return RUN_ALL_TESTS();
}
