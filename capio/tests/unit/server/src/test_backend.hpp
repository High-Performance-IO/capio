#ifndef CAPIO_SERVER_TESTS_TEST_BACKEND_HPP
#define CAPIO_SERVER_TESTS_TEST_BACKEND_HPP

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <semaphore.h>
#include <string>
#include <thread>
#include <vector>

#include "client-manager/client_manager.hpp"
#include "common/constants.hpp"
#include "common/queue.hpp"
#include "remote/backend.hpp"
#include "utils/capiocl_adapter.hpp"

extern Backend *backend;
extern ClientManager *client_manager;

/**
 * @brief A backend that records what the server sends, from any thread, and gives recv_file the
 * bytes of @ref incoming_file
 */
class RecordingBackend : public Backend {
    std::mutex mutex;
    std::condition_variable sent;
    std::vector<std::pair<std::string, std::string>> messages; // (request, target)
    std::vector<std::string> files;

  public:
    std::string incoming_file;

    RecordingBackend() : Backend(256) {}

    void handshake_servers() override {}
    RemoteRequest read_next_request() override { return {std::string{}, std::string{}}; }
    void send_request(const char *message, int size, const std::string &target) override {
        const std::lock_guard lock(mutex);
        messages.emplace_back(std::string(message, strnlen(message, size)), target);
        sent.notify_all();
    }
    void send_file(const char *message, int size, char *data, long int nbytes,
                   const std::string &target) override {
        const std::lock_guard lock(mutex);
        files.emplace_back(data == nullptr ? std::string() : std::string(data, nbytes));
        messages.emplace_back(std::string(message, strnlen(message, size)), target);
        sent.notify_all();
    }
    void recv_file(char *data, long int nbytes, const std::string &) override {
        ASSERT_EQ(nbytes, static_cast<long int>(incoming_file.size()));
        std::memcpy(data, incoming_file.data(), nbytes);
    }
    void connect_to(const std::string &) override {}

    /// @brief The message sent @p index-th, waiting for it up to @p timeout
    std::optional<std::pair<std::string, std::string>>
    message(const size_t index, const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock lock(mutex);
        if (!sent.wait_for(lock, timeout, [&] { return messages.size() > index; })) {
            return std::nullopt;
        }
        return messages[index];
    }

    /// @brief The file data of the @p index-th send_file
    std::string file(const size_t index) {
        const std::lock_guard lock(mutex);
        return index < files.size() ? files[index] : std::string("<none>");
    }

    size_t count() {
        const std::lock_guard lock(mutex);
        return messages.size();
    }
};

/// @brief Makes @p replacement the server's backend for the scope
class ScopedTestBackend {
  public:
    explicit ScopedTestBackend(Backend *replacement) {
        EXPECT_EQ(backend, nullptr);
        backend = replacement;
    }
    ~ScopedTestBackend() { backend = nullptr; }
    ScopedTestBackend(const ScopedTestBackend &)            = delete;
    ScopedTestBackend &operator=(const ScopedTestBackend &) = delete;
};

/// @brief A registered client, seen from its side of the response queue
class TestClient {
    CircularBuffer<off_t> *responses = nullptr;
    sem_t *available                 = nullptr;

  public:
    const pid_t tid;

    explicit TestClient(const pid_t tid) : tid(tid) {
        client_manager->registerClient(tid, "test-client");
        const std::string channel = SHM_COMM_CHAN_NAME_RESP + std::to_string(tid);
        const auto &workflow_name = CapioCLEngine::get().getWorkflowName();
        // Not deleted: its destructor would unlink the shared memory that client_manager owns
        responses =
            new CircularBuffer<off_t>(channel, CAPIO_REQ_BUFF_CNT, sizeof(off_t), workflow_name);
        available = sem_open((workflow_name + SHM_SEM_ELEMS + channel).c_str(), 0);
        EXPECT_NE(available, SEM_FAILED);
    }

    ~TestClient() {
        while (pending() > 0) {
            off_t ignored;
            responses->read(&ignored);
        }
        sem_close(available);
        client_manager->removeClient(tid);
    }

    TestClient(const TestClient &)            = delete;
    TestClient &operator=(const TestClient &) = delete;

    int pending() const {
        int value = 0;
        sem_getvalue(available, &value);
        return value;
    }

    /// @brief The next reply to the client, or nullopt if none arrives within @p timeout
    std::optional<off_t> reply(const std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (pending() == 0) {
            if (std::chrono::steady_clock::now() > deadline) {
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        off_t value;
        responses->read(&value);
        return value;
    }
};

#endif // CAPIO_SERVER_TESTS_TEST_BACKEND_HPP
