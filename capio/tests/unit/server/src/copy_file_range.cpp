#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <optional>
#include <semaphore.h>
#include <string>
#include <thread>
#include <vector>

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "common/requests.hpp"
#include "remote/backend.hpp"
#include "remote/home_node_service.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"

#include "handlers/copy_file_range.hpp"
#include "handlers/write.hpp"
#include "remote/handlers/copy_file_range.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;
extern capiocl::engine::Engine *capio_cl_engine;
extern Backend *backend;

TEST(CapioFileTest, copyFromFileCopiesBetweenTwoFiles) {
    CapioFile src, dst;
    src.createBuffer("copy_src.dat", false);
    dst.createBuffer("copy_dst.dat", false);
    src.expandBuffer(8);
    dst.expandBuffer(8);
    memcpy(src.getBuffer(), "abcdefgh", 8);
    memcpy(dst.getBuffer(), "12345678", 8);

    dst.copyFromFile(src, 2, 4, 3);
    EXPECT_EQ(std::string(dst.getBuffer(), 8), "1234cde8");
}

TEST(CapioFileTest, copyFromFileCopiesWithinTheSameFile) {
    CapioFile file;
    file.createBuffer("copy_same.dat", false);
    file.expandBuffer(8);
    memcpy(file.getBuffer(), "abcd0000", 8);

    file.copyFromFile(file, 0, 4, 4);
    EXPECT_EQ(std::string(file.getBuffer(), 8), "abcdabcd");
}

namespace {

// Records what this server sends to the other servers instead of sending it, and gives the data
// set in `incoming` to the server that receives data from another one.
class RecordingBackend : public Backend {
  public:
    RecordingBackend() : Backend(256) {}

    std::vector<std::string> requests; ///< Messages sent, with or without data
    std::vector<std::string> data;     ///< Data sent with the messages of send_file
    std::vector<std::string> targets;  ///< Nodes the messages are sent to
    std::string incoming;              ///< Data received by recv_file

    void handshake_servers() override {}
    RemoteRequest read_next_request() override { return {{}, {}}; }
    void send_file(const char *message, int, char *file, long int nbytes,
                   const std::string &target) override {
        requests.emplace_back(message);
        data.emplace_back(file == nullptr ? "" : std::string(file, nbytes));
        targets.push_back(target);
    }
    void recv_file(char *shm, long int bytes_expected, const std::string &) override {
        memcpy(shm, incoming.data(), bytes_expected);
    }
    void send_request(const char *message, int, const std::string &target) override {
        requests.emplace_back(message);
        targets.push_back(target);
    }
    void connect_to(const std::string &) override {}
};

} // namespace

/*
 * copy_file_range between two CAPIO files, done by the server: a producer writes the input, and a
 * copier copies it into the output with copy_file_range requests.
 */
class CopyFileRangeTest : public testing::Test {
  protected:
    static constexpr pid_t PRODUCER_TID = 5151;
    static constexpr pid_t COPIER_TID   = 5252;
    static constexpr int FD_IN          = 3;
    static constexpr int FD_OUT         = 4;
    static constexpr char REMOTE_NODE[] = "capio-copy-file-range-remote-node";

    std::string path_in, path_out;
    // Attaches to the copier's response channel, the one the POSIX client reads replies from.
    // Not deleted: the destructor would unlink the shared memory still owned by client_manager.
    CircularBuffer<off_t> *responses = nullptr;
    sem_t *responses_available       = nullptr;
    RecordingBackend test_backend;

    void SetUp() override {
        ASSERT_EQ(backend, nullptr);
        backend = &test_backend;

        // Fresh files for every run: with --gtest_repeat the previous run's files are still in
        // the storage manager.
        static int run         = 0;
        const std::string name = testing::UnitTest::GetInstance()->current_test_info()->name();
        const std::string id   = name + "_" + std::to_string(run++) + ".dat";
        path_in                = "copy_file_range_in_" + id;
        path_out               = "copy_file_range_out_" + id;
        capio_cl_engine->setCommitRule(path_in, capiocl::commitRules::ON_CLOSE);
        capio_cl_engine->setFireRule(path_in, capiocl::fireRules::NO_UPDATE);

        client_manager->registerClient(PRODUCER_TID, "producer");
        client_manager->registerClient(COPIER_TID, "copier");
        // Skip the first-write bookkeeping (announcing the file location) that a write would do.
        storage_manager->add(path_in, false, 0).registerFirstWrite();
        storage_manager->add(path_out, false, 0).registerFirstWrite();
        storage_manager->addFileToTid(PRODUCER_TID, FD_IN, path_in, 0);
        storage_manager->addFileToTid(COPIER_TID, FD_IN, path_in, 0);
        storage_manager->addFileToTid(COPIER_TID, FD_OUT, path_out, 0);

        const std::string channel = SHM_COMM_CHAN_NAME_RESP + std::to_string(COPIER_TID);
        const auto &workflow_name = CapioCLEngine::get().getWorkflowName();
        responses =
            new CircularBuffer<off_t>(channel, CAPIO_REQ_BUFF_CNT, sizeof(off_t), workflow_name);
        responses_available = sem_open((workflow_name + SHM_SEM_ELEMS + channel).c_str(), 0);
        ASSERT_NE(responses_available, SEM_FAILED);
    }

    void TearDown() override {
        // Release any copy still waiting on the input before its client goes away.
        storage_manager->get(path_in).setCommitted();
        while (pending_responses() > 0) {
            off_t ignored;
            responses->read(&ignored);
        }
        // Give a copy released by the commit above time to finish before its files are removed.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        sem_close(responses_available);
        home_node_service->remove(path_in);
        storage_manager->remove(path_in);
        storage_manager->remove(path_out);
        client_manager->removeClient(PRODUCER_TID);
        client_manager->removeClient(COPIER_TID);
        backend = nullptr;
    }

    // The input is stored on this node.
    void input_is_local() { home_node_service->learn(path_in, backend->get_node_name()); }

    // The input is stored on another node.
    void input_is_remote() { home_node_service->learn(path_in, REMOTE_NODE); }

    int pending_responses() const {
        int value = 0;
        sem_getvalue(responses_available, &value);
        return value;
    }

    // The next reply to the copier, or nullopt if none arrives within the timeout.
    std::optional<off_t> next_response(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (pending_responses() == 0) {
            if (std::chrono::steady_clock::now() > deadline) {
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        off_t value;
        responses->read(&value);
        return value;
    }

    // What a producer's write() of `data` does on the server side.
    void produce(const std::string &data) {
        client_manager->getClientToServerDataBuffers(PRODUCER_TID).write(data.data(), data.size());
        char request[CAPIO_REQ_MAX_SIZE];
        sprintf(request, "%d %d %ld", PRODUCER_TID, FD_IN, static_cast<long>(data.size()));
        write_handler(request);
    }

    // What the copier's copy_file_range request does on the server side.
    void copy(const off64_t offset_in, const off64_t offset_out, const off64_t count) {
        char request[CAPIO_REQ_MAX_SIZE];
        sprintf(request, "%d %d %d %ld %ld %ld", COPIER_TID, FD_IN, FD_OUT, offset_in, offset_out,
                count);
        copy_file_range_handler(request);
    }

    std::string output(const off64_t count) const {
        return {storage_manager->get(path_out).getBuffer(), static_cast<size_t>(count)};
    }
};

TEST_F(CopyFileRangeTest, copyBetweenLocalFilesCopiesTheData) {
    input_is_local();
    produce("abcdefgh");

    copy(2, 0, 4);
    EXPECT_EQ(next_response(std::chrono::seconds(2)), 4);
    EXPECT_EQ(output(4), "cdef");

    // The copied bytes are data of the output, and the copier is its producer.
    EXPECT_EQ(storage_manager->get(path_out).getSectorEnd(0), 4);
    EXPECT_TRUE(client_manager->isProducer(COPIER_TID, path_out));

    // The client sets the file offsets of the descriptors: the server leaves them.
    EXPECT_EQ(storage_manager->getFileOffset(COPIER_TID, FD_IN), 0);
    EXPECT_EQ(storage_manager->getFileOffset(COPIER_TID, FD_OUT), 0);
}

TEST_F(CopyFileRangeTest, copyWaitsForTheDataOfTheInput) {
    input_is_local();
    produce("abcd");

    // Only half of the requested bytes are there, and the input is not committed: as a read, the
    // copy waits for the rest.
    copy(0, 0, 8);
    EXPECT_EQ(next_response(std::chrono::milliseconds(200)), std::nullopt)
        << "copy of a not yet written range was answered before its data arrived";

    produce("efgh");
    EXPECT_EQ(next_response(std::chrono::seconds(2)), 8);
    EXPECT_EQ(output(8), "abcdefgh");
}

TEST_F(CopyFileRangeTest, copyOfInputCommittedWithoutDataReturnsEndOfInput) {
    input_is_local();
    copy(0, 0, 8);

    storage_manager->get(path_in).setCommitted();

    EXPECT_EQ(next_response(std::chrono::seconds(2)), 0);
}

TEST_F(CopyFileRangeTest, copyOfARemoteInputAsksItsNodeForTheData) {
    input_is_remote();
    storage_manager->setFileOffset(COPIER_TID, FD_IN, 3);

    copy(2, 0, 4);

    // The node that stores the input is asked for its data at the position of the copy, with the
    // descriptors and positions it sends back with the data.
    ASSERT_EQ(test_backend.requests.size(), 1);
    EXPECT_EQ(test_backend.targets[0], REMOTE_NODE);
    char expected[CAPIO_REQ_MAX_SIZE];
    sprintf(expected, "%04d %s %d %d %d %ld %ld %ld", CAPIO_SERVER_REQUEST_COPY_FILE_RANGE,
            path_in.c_str(), COPIER_TID, FD_IN, FD_OUT, 2L, 0L, 4L);
    EXPECT_EQ(test_backend.requests[0], expected);

    // The client waits for the data, and no file offset moved.
    EXPECT_EQ(next_response(std::chrono::milliseconds(200)), std::nullopt);
    EXPECT_EQ(storage_manager->getFileOffset(COPIER_TID, FD_IN), 3);
}

TEST_F(CopyFileRangeTest, copyOfARemoteInputCopiesTheDataItsNodeSends) {
    input_is_remote();

    // The node that stores the input sends 6 bytes from offset 2 of the committed input.
    test_backend.incoming = "cdefgh";
    handle_remote_copy_file_range_reply(REMOTE_NODE, COPIER_TID, FD_IN, FD_OUT, 2, 0, 4, 6, 8,
                                        true);

    EXPECT_EQ(next_response(std::chrono::seconds(2)), 4);
    EXPECT_EQ(output(4), "cdef");
    // The data stays in the copy of the input kept on this node, for the next copies and reads.
    const CapioFile &c_file_in = storage_manager->get(path_in);
    EXPECT_EQ(c_file_in.getSectorEnd(2), 8);
    EXPECT_TRUE(c_file_in.isCommitted());
}

TEST_F(CopyFileRangeTest, nodeOfTheInputSendsItsDataForACopy) {
    input_is_local();
    produce("abcdefgh");

    handle_remote_copy_file_range_data(path_in, "client-node", COPIER_TID, FD_IN, FD_OUT, 2, 0, 4);

    // As for a read, all the data from the position of the copy is sent, and not only the bytes
    // requested.
    ASSERT_EQ(test_backend.requests.size(), 1);
    EXPECT_EQ(test_backend.targets[0], "client-node");
    char expected[CAPIO_REQ_MAX_SIZE];
    sprintf(expected, "%04d %d %d %d %ld %ld %ld %ld %ld %d",
            CAPIO_SERVER_REQUEST_COPY_FILE_RANGE_REPLY, COPIER_TID, FD_IN, FD_OUT, 2L, 0L, 4L, 6L,
            8L, 0);
    EXPECT_EQ(test_backend.requests[0], expected);
    EXPECT_EQ(test_backend.data[0], "cdefgh");
}

TEST_F(CopyFileRangeTest, nodeOfTheInputWaitsForTheDataOfACopy) {
    input_is_local();
    produce("abcd");

    // The range of the copy is not all there, and the input is not committed: as for a read, the
    // data is sent once it arrives.
    handle_remote_copy_file_range_data(path_in, "client-node", COPIER_TID, FD_IN, FD_OUT, 2, 0, 4);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(test_backend.requests.empty());

    produce("efgh");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (test_backend.data.empty() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(test_backend.data.size(), 1);
    EXPECT_EQ(test_backend.data[0], "cdefgh");
}

TEST_F(CopyFileRangeTest, copyOfARemoteInputAlreadyOnThisNodeSendsNoRequest) {
    input_is_remote();
    // A previous read already brought the whole committed input to this node.
    CapioFile &c_file_in = storage_manager->get(path_in);
    c_file_in.createBuffer(path_in, false);
    c_file_in.expandBuffer(8);
    memcpy(c_file_in.getBuffer(), "abcdefgh", 8);
    c_file_in.insertSector(0, 8);
    c_file_in.setRealFileSize(8);
    c_file_in.setCommitted();

    copy(4, 0, 100);
    EXPECT_EQ(next_response(std::chrono::seconds(2)), 4);
    EXPECT_EQ(output(4), "efgh");
    EXPECT_TRUE(test_backend.requests.empty());
}
