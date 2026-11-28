#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <future>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "child.hpp"
#include "client-manager/request_manager.hpp"
#include "common/requests.hpp"
#include "handlers.hpp"
#include "remote/backend/none.hpp"
#include "storage/manager.hpp"
#include "test_backend.hpp"
#include "utils/waiter.hpp"

extern Backend *backend;
extern StorageManager *storage_manager;

TEST(ClientRequestManagerTest, EachRequestHasItsHandler) {
    for (int code = 0; code < CAPIO_NR_REQUESTS; ++code) {
        EXPECT_NE(ClientRequestManager::handlerOf(code), nullptr) << "request " << code;
    }
    EXPECT_EQ(ClientRequestManager::handlerOf(CAPIO_REQUEST_ACCESS), &access_handler);
    EXPECT_EQ(ClientRequestManager::handlerOf(CAPIO_REQUEST_SEEK), &lseek_handler);
    // Two requests, one handler: the handler reads the size of the entries from the request
    EXPECT_EQ(ClientRequestManager::handlerOf(CAPIO_REQUEST_GETDENTS), &getdents_handler);
    EXPECT_EQ(ClientRequestManager::handlerOf(CAPIO_REQUEST_GETDENTS64), &getdents_handler);
    EXPECT_EQ(ClientRequestManager::handlerOf(CAPIO_REQUEST_PATH_PART), &path_part_handler);
    EXPECT_EQ(ClientRequestManager::handlerOf(-1), nullptr);
    EXPECT_EQ(ClientRequestManager::handlerOf(CAPIO_NR_REQUESTS), nullptr);
}

TEST(ClientRequestManagerTest, ACodeThatIsNotARequestIsRefused) {
    EXPECT_FALSE(ClientRequestManager::handle(-1, ""));
    EXPECT_FALSE(ClientRequestManager::handle(CAPIO_NR_REQUESTS, ""));
}

namespace {

class ClientRequestDispatchTest : public testing::Test {
  protected:
    NoneBackend none_backend{0, nullptr};

    /// @brief The paths the storage had before the test: the others are removed after it
    std::vector<std::filesystem::path> paths_before;

    void SetUp() override {
        ASSERT_EQ(backend, nullptr);
        backend      = &none_backend;
        paths_before = storage_manager->getPaths();
    }

    void TearDown() override {
        for (const auto &path : storage_manager->getPaths()) {
            if (std::find(paths_before.begin(), paths_before.end(), path) == paths_before.end()) {
                storage_manager->remove(path);
            }
        }
        backend = nullptr;
    }

    static std::string request(const pid_t tid, const std::string &text) {
        return std::to_string(tid) + " " + text;
    }
};

} // namespace

TEST_F(ClientRequestDispatchTest, ARequestGoesToItsHandler) {
    TestClient client(6101);
    const std::string directory = "/request_manager_directory";

    // access of a path no server has: -1
    EXPECT_TRUE(
        ClientRequestManager::handle(CAPIO_REQUEST_ACCESS, request(client.tid, directory).c_str()));
    EXPECT_EQ(client.reply(), -1);

    EXPECT_TRUE(
        ClientRequestManager::handle(CAPIO_REQUEST_MKDIR, request(client.tid, directory).c_str()));
    EXPECT_EQ(client.reply(), 0);
    EXPECT_TRUE(
        ClientRequestManager::handle(CAPIO_REQUEST_ACCESS, request(client.tid, directory).c_str()));
    EXPECT_EQ(client.reply(), 0);

    EXPECT_TRUE(
        ClientRequestManager::handle(CAPIO_REQUEST_RMDIR, request(client.tid, directory).c_str()));
    EXPECT_TRUE(client.reply().has_value());
    EXPECT_TRUE(
        ClientRequestManager::handle(CAPIO_REQUEST_ACCESS, request(client.tid, directory).c_str()));
    EXPECT_EQ(client.reply(), -1);
}

TEST_F(ClientRequestDispatchTest, TheBeginningOfALongPathComesInPathParts) {
    TestClient client(6102);
    // The path of the access is "/request_manager_" + "long_path": the part arrives first
    EXPECT_TRUE(ClientRequestManager::handle(CAPIO_REQUEST_PATH_PART,
                                             request(client.tid, "/request_manager_").c_str()));
    EXPECT_EQ(client.pending(), 0); // no reply to a part
    EXPECT_TRUE(ClientRequestManager::handle(CAPIO_REQUEST_MKDIR,
                                             request(client.tid, "long_path").c_str()));
    EXPECT_EQ(client.reply(), 0);
    EXPECT_TRUE(storage_manager->tryGet("/request_manager_long_path"));
}

TEST(WaiterTest, AWaiterDoesNotGetTheTerminationSignals) {
    std::promise<sigset_t> waiter_mask;
    spawn_waiter([&waiter_mask] {
        sigset_t mask;
        pthread_sigmask(SIG_BLOCK, nullptr, &mask);
        waiter_mask.set_value(mask);
    });
    const sigset_t mask = waiter_mask.get_future().get();
    for (const int signal : {SIGTERM, SIGINT, SIGQUIT, SIGPIPE}) {
        EXPECT_EQ(sigismember(&mask, signal), 1) << strsignal(signal);
    }

    // The caller gets them again
    sigset_t caller;
    pthread_sigmask(SIG_BLOCK, nullptr, &caller);
    EXPECT_EQ(sigismember(&caller, SIGTERM), 0);
}

TEST(WaiterTest, AWaiterGetsItsArguments) {
    std::promise<int> sum;
    spawn_waiter(
        [](std::promise<int> *result, const int a, const int b) { result->set_value(a + b); }, &sum,
        40, 2);
    EXPECT_EQ(sum.get_future().get(), 42);
}

namespace {

/// @brief Stop the requests, and check that one sent afterwards is not handled. It never ends, so
/// the child that runs this exits without returning.
[[noreturn]] void stop_and_send_a_request() {
    TestClient client(6103);
    EXPECT_TRUE(ClientRequestManager::stop(std::chrono::milliseconds(100)));
    auto handled = std::async(std::launch::async, [&client] {
        return ClientRequestManager::handle(
            CAPIO_REQUEST_ACCESS,
            (std::to_string(client.tid) + " /request_manager_stopped").c_str());
    });
    EXPECT_EQ(handled.wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);
    EXPECT_EQ(client.pending(), 0);
    std::fflush(nullptr);
#ifdef CAPIO_COVERAGE
    __gcov_dump(); // _exit does not write it
#endif
    _exit(testing::Test::HasFailure() ? 1 : 0);
}

} // namespace

TEST_F(ClientRequestDispatchTest, AfterStopNoRequestIsHandled) {
    // stop() holds the requests for good, as the teardown of the server does: in a child
    EXPECT_EQ(run_in_child(stop_and_send_a_request), 0);
}
