#ifndef CAPIO_SERVER_READ_TEST_HPP
#define CAPIO_SERVER_READ_TEST_HPP
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <semaphore.h>
#include <thread>

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "common/requests.hpp"
#include "remote/backend/none.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/location.hpp"

#include "handlers/read.hpp"
#include "handlers/write.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;
extern capiocl::engine::Engine *capio_cl_engine;
extern Backend *backend;

/*
 * Streaming reads of a no_update (FnU) file that is not committed yet. A consumer started together
 * with its producer may ask for data before the first byte is written: the server must hold the
 * read until data (or the commit) arrives instead of answering it as end of file.
 */
class StreamingReadTest : public testing::Test {
  protected:
    static constexpr pid_t PRODUCER_TID = 4242;
    static constexpr pid_t CONSUMER_TID = 4343;
    static constexpr int FD             = 3;

    std::string path;
    // Attaches to the consumer's response channel, the one the POSIX client reads replies from.
    // Not deleted: the destructor would unlink the shared memory still owned by client_manager.
    CircularBuffer<off_t> *responses = nullptr;
    sem_t *responses_available       = nullptr;

    void SetUp() override {
        ASSERT_EQ(backend, nullptr);
        backend = &test_backend;

        // A fresh file for every run: with --gtest_repeat the previous run's file is still in
        // the storage manager, committed.
        static int run = 0;
        path           = std::string("streaming_read_") +
               testing::UnitTest::GetInstance()->current_test_info()->name() + "_" +
               std::to_string(run++) + ".dat";
        capio_cl_engine->setCommitRule(path, capiocl::commitRules::ON_CLOSE);
        capio_cl_engine->setFireRule(path, capiocl::fireRules::NO_UPDATE);

        client_manager->registerClient(PRODUCER_TID, "producer");
        client_manager->registerClient(CONSUMER_TID, "consumer");
        CapioFile &c_file = storage_manager->add(path, false, 0);
        // The producer created the file: skip the first-write bookkeeping (file location files)
        // that write_handler would otherwise do.
        c_file.registerFirstWrite();
        storage_manager->addFileToTid(PRODUCER_TID, FD, path, 0);
        storage_manager->addFileToTid(CONSUMER_TID, FD, path, 0);

        const std::string channel = SHM_COMM_CHAN_NAME_RESP + std::to_string(CONSUMER_TID);
        const auto &workflow_name = CapioCLEngine::get().getWorkflowName();
        responses =
            new CircularBuffer<off_t>(channel, CAPIO_REQ_BUFF_CNT, sizeof(off_t), workflow_name);
        responses_available = sem_open((workflow_name + SHM_SEM_ELEMS + channel).c_str(), 0);
        ASSERT_NE(responses_available, SEM_FAILED);
    }

    void TearDown() override {
        // Release any read still waiting on the file before its client goes away.
        storage_manager->get(path).setCommitted();
        while (pending_responses() > 0) {
            off_t ignored;
            responses->read(&ignored);
        }
        // A handler thread replies before it moves the file offset: give a reply released by
        // the commit above time to finish before the file it uses is removed.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        sem_close(responses_available);
        // Leave the shared storage as it was: other suites count the files it holds.
        storage_manager->remove(path);
        client_manager->removeClient(PRODUCER_TID);
        client_manager->removeClient(CONSUMER_TID);
        backend = nullptr;
    }

    // Whether the consumer's file offset reaches `expected`: the handler thread that replied
    // moves the offset right after the reply.
    bool offset_reaches(const off64_t expected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (storage_manager->getFileOffset(CONSUMER_TID, FD) != expected) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return true;
    }

    int pending_responses() const {
        int value = 0;
        sem_getvalue(responses_available, &value);
        return value;
    }

    // The next reply to the consumer, or nullopt if none arrives within the timeout.
    std::optional<off_t> next_response(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (pending_responses() == 0) {
            if (std::chrono::steady_clock::now() > deadline) {
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        off_t offset;
        responses->read(&offset);
        return offset;
    }

    // What a producer's write() of `count` bytes does on the server side.
    void produce(const off64_t count) {
        const std::vector<char> data(count, 'x');
        client_manager->getClientToServerDataBuffers(PRODUCER_TID).write(data.data(), count);
        char request[CAPIO_REQ_MAX_SIZE];
        sprintf(request, "%d %d %ld", PRODUCER_TID, FD, count);
        write_handler(request);
    }

  private:
    NoneBackend test_backend{0, nullptr};
};

TEST_F(StreamingReadTest, readBeforeFirstWriteWaitsForData) {
    // The consumer asks for data before the producer wrote anything.
    handle_local_read(CONSUMER_TID, FD, 8, false);

    // Answering now could only mean end of file, which the file has not reached.
    EXPECT_EQ(next_response(std::chrono::milliseconds(200)), std::nullopt)
        << "read of a not yet written no_update file was answered as end of file";

    produce(8);

    // The reply carries the offset after the read: all 8 bytes are served.
    EXPECT_EQ(next_response(std::chrono::seconds(2)), 8);
    EXPECT_TRUE(offset_reaches(8));
}

TEST_F(StreamingReadTest, waitingReadIsServedByTheWriteThatCompletesIt) {
    produce(8);

    // Only half of the requested bytes are there: the read waits for the rest.
    handle_local_read(CONSUMER_TID, FD, 16, false);
    EXPECT_EQ(next_response(std::chrono::milliseconds(200)), std::nullopt);

    // This write makes the data available: the read must not wait for a later write or the
    // commit.
    produce(8);
    EXPECT_EQ(next_response(std::chrono::seconds(2)), 16);
    EXPECT_TRUE(offset_reaches(16));
}

TEST(CapioFileTest, insertSectorWakesThreadsWaitingForData) {
    CapioFile c_file;
    c_file.insertSector(0, 8);

    std::atomic<bool> woken = false;
    std::thread reader([&] {
        c_file.waitForData(16);
        woken = true;
    });
    // Let the reader block on the missing bytes before they arrive.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_FALSE(woken);

    // Recording the bytes is what makes them available: it alone must wake the reader.
    c_file.insertSector(8, 16);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!woken && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(woken) << "a thread waiting for data was not woken when the data was stored";

    c_file.setCommitted(); // releases the reader if it was never woken
    reader.join();
}

TEST_F(StreamingReadTest, readOfFileCommittedWithoutDataReturnsEndOfFile) {
    handle_local_read(CONSUMER_TID, FD, 8, false);

    storage_manager->get(path).setCommitted();

    // The producer closed the file without writing: the read ends at offset 0 (end of file).
    EXPECT_EQ(next_response(std::chrono::seconds(2)), 0);
    EXPECT_EQ(storage_manager->getFileOffset(CONSUMER_TID, FD), 0);
}

#endif // CAPIO_SERVER_READ_TEST_HPP
