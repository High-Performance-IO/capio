#ifndef CAPIO_SERVER_OPEN_TEST_HPP
#define CAPIO_SERVER_OPEN_TEST_HPP
#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <optional>
#include <semaphore.h>
#include <string>
#include <thread>
#include <vector>

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "common/requests.hpp"
#include "remote/backend/none.hpp"
#include "remote/home_node_service.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"

#include "handlers.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;
extern capiocl::engine::Engine *capio_cl_engine;
extern Backend *backend;
extern HomeNodeService *home_node_service;

namespace {

// Two other servers whose requests are recorded and never answered.
class SilentPeersBackend : public Backend {
    std::mutex _mutex;
    std::vector<std::string> _sent;

  public:
    SilentPeersBackend() : Backend(256) {}

    const std::set<std::string> get_nodes() override { return {node_name, "peer-a", "peer-b"}; }
    void handshake_servers() override {}
    RemoteRequest read_next_request() override { return {std::string{}, std::string{}}; }
    void send_file(const char *, int, char *, long int, const std::string &) override {}
    void recv_file(char *, long int, const std::string &) override {}
    void connect_to(const std::string &) override {}
    void send_request(const char *message, int, const std::string &) override {
        const std::lock_guard lg(_mutex);
        _sent.emplace_back(message);
    }

    std::vector<std::string> sent() {
        const std::lock_guard lg(_mutex);
        return _sent;
    }
};

} // namespace

/*
 * A producer opens, without creating it, a file it has not created yet. CFITSIO does so before it
 * creates a file, to check that the file does not exist (fopen(path, "r")), so every Montage tool
 * that writes FITS opens its own output this way first. Nothing else will create the file
 * meanwhile: the server must answer ENOENT at once, not wait for the file.
 */
class ProducerOpenTest : public testing::Test {
  protected:
    static constexpr pid_t PRODUCER_TID = 5151;
    static constexpr int FD             = 3;

    std::string producer = "producer";
    std::string path;
    CircularBuffer<off_t> *responses = nullptr;
    sem_t *responses_available       = nullptr;

    void SetUp() override {
        ASSERT_EQ(backend, nullptr);
        backend = &test_backend;

        // A fresh file for every run, which no previous run has created.
        static int run = 0;
        path           = std::string("producer_open_") +
                         testing::UnitTest::GetInstance()->current_test_info()->name() + "_" +
                         std::to_string(run++) + ".fits";
        capio_cl_engine->newFile(path);
        capio_cl_engine->addProducer(path, producer);

        client_manager->registerClient(PRODUCER_TID, producer);
        const std::string channel = SHM_COMM_CHAN_NAME_RESP + std::to_string(PRODUCER_TID);
        const auto &workflow_name = CapioCLEngine::get().getWorkflowName();
        responses =
            new CircularBuffer<off_t>(channel, CAPIO_REQ_BUFF_CNT, sizeof(off_t), workflow_name);
        responses_available = sem_open((workflow_name + SHM_SEM_ELEMS + channel).c_str(), 0);
        ASSERT_NE(responses_available, SEM_FAILED);
    }

    void TearDown() override {
        sem_close(responses_available);
        client_manager->removeClient(PRODUCER_TID);
        backend = nullptr;
    }

    int pending_responses() const {
        int value = 0;
        sem_getvalue(responses_available, &value);
        return value;
    }

    // The next reply to the producer, or nullopt if none arrives within the timeout.
    std::optional<off_t> next_response(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (pending_responses() == 0) {
            if (std::chrono::steady_clock::now() > deadline) {
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        off_t reply;
        responses->read(&reply);
        return reply;
    }

  private:
    NoneBackend test_backend{0, nullptr};
};

TEST_F(ProducerOpenTest, producerOpeningAFileItHasNotCreatedGetsEnoent) {
    handle_open(PRODUCER_TID, FD, path);

    // 1 is the reply the POSIX client turns into ENOENT (capio_openat).
    EXPECT_EQ(next_response(std::chrono::seconds(2)), 1)
        << "a producer's open of a file it has not created waited for the file instead of failing";
    EXPECT_FALSE(storage_manager->tryGet(path)) << "the failed open created the file";
}

TEST_F(ProducerOpenTest, producerCreatingItsFileDoesNotAskTheOtherServers) {
    // The other servers never answer: asking them would hold this server's request thread, and
    // with it every client of this server, until the location query times out.
    SilentPeersBackend peers;
    backend = &peers;
    HomeNodeService service{std::chrono::milliseconds(500), std::chrono::milliseconds(500)};
    HomeNodeService *global_service = home_node_service;
    home_node_service               = &service;

    handle_create(PRODUCER_TID, FD, path);

    home_node_service = global_service;
    EXPECT_EQ(next_response(std::chrono::seconds(2)), 0);
    for (const auto &message : peers.sent()) {
        EXPECT_NE(std::stoi(message.substr(0, 4)), CAPIO_SERVER_REQUEST_LOCATION_QUERY)
            << "the declared producer of a file asked the other servers where it is: " << message;
    }
    storage_manager->remove(path);
}

#endif // CAPIO_SERVER_OPEN_TEST_HPP
