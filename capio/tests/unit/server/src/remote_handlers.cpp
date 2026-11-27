#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "storage/manager.hpp"
#include "test_backend.hpp"

#include "remote/handlers/read.hpp"
#include "remote/handlers/stat.hpp"

extern StorageManager *storage_manager;
extern capiocl::engine::Engine *capio_cl_engine;

/*
 * The requests one server sends another, as the remote listener hands them to their handlers. The
 * paths hold spaces: they are the last field of each request, so that they can.
 */
class RemoteHandlersTest : public testing::Test {
  protected:
    static constexpr pid_t TID = 5151;
    static constexpr int FD    = 7;
    RecordingBackend recording;
    ScopedTestBackend scoped{&recording};
    std::string path;

    void SetUp() override {
        static int run = 0;
        path           = std::string("/remote handlers/") +
                         testing::UnitTest::GetInstance()->current_test_info()->name() + " " +
                         std::to_string(run++);
    }

    void TearDown() override {
        if (const auto c_file = storage_manager->tryGet(path)) {
            c_file->get().setCommitted(); // releases the threads still waiting on the file
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            storage_manager->remove(path);
        }
    }

    /// @brief A file of this node holding @p data
    CapioFile &file_with(const std::string &data, const bool committed) {
        CapioFile &c_file = storage_manager->add(path, false, 0);
        c_file.createBuffer(path, true);
        append(c_file, data);
        c_file.setCommitted(committed);
        return c_file;
    }

    static void append(CapioFile &c_file, const std::string &data) {
        const off64_t start = c_file.getStoredSize();
        c_file.expandBuffer(start + static_cast<off64_t>(data.size()));
        std::memcpy(c_file.getBuffer() + start, data.data(), data.size());
        c_file.insertSector(start, start + static_cast<off64_t>(data.size()));
    }

    static RemoteRequest request(const int code, const std::string &content) {
        char prefix[6];
        snprintf(prefix, sizeof(prefix), "%04d ", code);
        return {std::string(prefix) + content, std::string("peer")};
    }
};

TEST_F(RemoteHandlersTest, AStatOfACommittedFileIsAnsweredAtOnce) {
    file_with("12345", true);
    remote_stat_handler(request(CAPIO_SERVER_REQUEST_STAT, "33 " + path));

    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, "0003 33 5 0 " + path);
    EXPECT_EQ(sent->second, "peer");
}

TEST_F(RemoteHandlersTest, AStatOfAFileBeingWrittenWaitsForItsCommit) {
    CapioFile &c_file = file_with("12", false);
    remote_stat_handler(request(CAPIO_SERVER_REQUEST_STAT, "33 " + path));
    EXPECT_EQ(recording.message(0, std::chrono::milliseconds(100)), std::nullopt);

    append(c_file, "345");
    c_file.setCommitted();
    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, "0003 33 5 0 " + path);
}

TEST_F(RemoteHandlersTest, AStatOfAFileNotCreatedYetWaitsForIt) {
    remote_stat_handler(request(CAPIO_SERVER_REQUEST_STAT, "33 " + path));
    EXPECT_EQ(recording.message(0, std::chrono::milliseconds(100)), std::nullopt);

    storage_manager->get(path).setCommitted();
    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, "0003 33 0 0 " + path);
}

TEST_F(RemoteHandlersTest, MalformedRequestsAreIgnored) {
    remote_stat_handler(request(CAPIO_SERVER_REQUEST_STAT, "no-tid"));
    remote_stat_reply_handler(request(CAPIO_SERVER_REQUEST_STAT_REPLY, "33 5"));
    remote_read_handler(request(CAPIO_SERVER_REQUEST_READ, "33 7 2"));
    EXPECT_EQ(recording.message(0, std::chrono::milliseconds(50)), std::nullopt);
}

TEST_F(RemoteHandlersTest, AStatReplyGoesToTheClient) {
    TestClient client(TID);
    remote_stat_reply_handler(
        request(CAPIO_SERVER_REQUEST_STAT_REPLY, std::to_string(TID) + " 4096 1 " + path));
    EXPECT_EQ(client.reply(), 4096);
    EXPECT_EQ(client.reply(), 1);
}

TEST_F(RemoteHandlersTest, AReadOfACommittedFileSendsItsData) {
    file_with("0123456789", true);
    remote_read_handler(request(CAPIO_SERVER_REQUEST_READ, "33 7 4 2 0 " + path));

    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    // tid, fd, count, bytes sent (all from the offset on), file size, committed, getdents
    EXPECT_EQ(sent->first, "0001 33 7 4 8 10 1 0");
    EXPECT_EQ(recording.file(0), "23456789");
}

TEST_F(RemoteHandlersTest, AReadPastTheDataOfAFileBeingWrittenWaitsForIt) {
    CapioFile &c_file = file_with("01", false);
    remote_read_handler(request(CAPIO_SERVER_REQUEST_READ, "33 7 4 0 0 " + path));
    EXPECT_EQ(recording.message(0, std::chrono::milliseconds(100)), std::nullopt);

    append(c_file, "2345");
    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, "0001 33 7 4 6 6 0 0");
    EXPECT_EQ(recording.file(0), "012345");
}

TEST_F(RemoteHandlersTest, AReadOfAnEmptyCommittedFileSendsNoData) {
    file_with("", true);
    remote_read_handler(request(CAPIO_SERVER_REQUEST_READ, "33 7 4 0 0 " + path));
    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, "0001 33 7 4 0 0 1 0");
    EXPECT_EQ(recording.file(0), "");
}

TEST_F(RemoteHandlersTest, AReadReplyStoresTheDataAndAnswersTheClient) {
    TestClient client(TID);
    storage_manager->add(path, false, 0);
    storage_manager->addFileToTid(TID, FD, path, 2);
    recording.incoming_file = "abcdef";

    // count 4 of the 6 bytes sent from offset 2, of a committed file of 8 bytes
    remote_read_reply_handler(
        request(CAPIO_SERVER_REQUEST_READ_REPLY,
                std::to_string(TID) + " " + std::to_string(FD) + " 4 6 8 1 0"));
    EXPECT_EQ(client.reply(), 2 + 4);
    EXPECT_EQ(storage_manager->getFileOffset(TID, FD), 2 + 4);

    const CapioFile &c_file = storage_manager->get(path);
    EXPECT_TRUE(c_file.isCommitted());
    EXPECT_EQ(c_file.getRealFileSize(), 8);
    EXPECT_EQ(std::string(c_file.getBuffer() + 2, 6), "abcdef");
    storage_manager->removeFromTid(TID, FD);
}

TEST_F(RemoteHandlersTest, AReadReplyWithFewerBytesThanAskedAnswersWithWhatItHas) {
    TestClient client(TID);
    storage_manager->add(path, false, 0);
    storage_manager->addFileToTid(TID, FD, path, 0);
    recording.incoming_file = "ab";

    remote_read_reply_handler(
        request(CAPIO_SERVER_REQUEST_READ_REPLY,
                std::to_string(TID) + " " + std::to_string(FD) + " 10 2 2 1 0"));
    EXPECT_EQ(client.reply(), 2);
    storage_manager->removeFromTid(TID, FD);
}
