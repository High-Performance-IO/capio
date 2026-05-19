#include <gtest/gtest.h>

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "remote/backend/none.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/location.hpp"

capiocl::engine::Engine *capio_cl_engine = nullptr;
StorageManager *storage_manager          = nullptr;
ClientManager *client_manager            = nullptr;
Backend *backend                         = nullptr;

const capiocl::engine::Engine &CapioCLEngine::get() { return *capio_cl_engine; }

class MockBackend : public Backend {
public:
    MockBackend() : Backend(HOST_NAME_MAX) {}

    void recv_file(char *shm, const std::string &source, const long int bytes_expected) override {
        for (long int i = 0; i < bytes_expected; ++i) {
            shm[i] = 33 + (i % 93);
        }
    }

    const std::set<std::string> get_nodes() override { return {node_name}; }
    void handshake_servers() override {}
    RemoteRequest read_next_request() override { return {nullptr, ""}; }
    void send_file(char *shm, long int nbytes, const std::string &target) override {}
    void send_request(const char *message, int message_len, const std::string &target) override {}
};

class ServerUnitTestEnvironment : public testing::Environment {
  public:
    explicit ServerUnitTestEnvironment() = default;

    void SetUp() override {
        capio_cl_engine = new capiocl::engine::Engine(false);
        client_manager  = new ClientManager();
        storage_manager = new StorageManager();
        backend         = new MockBackend();
        open_files_location();
    }

    void TearDown() override {
        delete storage_manager;
        delete client_manager;
        delete capio_cl_engine;
        delete backend;
    }
};

int main(int argc, char **argv, char **envp) {
    testing::InitGoogleTest(&argc, argv);

    testing::AddGlobalTestEnvironment(new ServerUnitTestEnvironment());
    return RUN_ALL_TESTS();
}