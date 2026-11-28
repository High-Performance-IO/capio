#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "common/dirent.hpp"
#include "remote/home_node_service.hpp"
#include "storage/manager.hpp"
#include "test_backend.hpp"

#include "handlers/getdents.hpp"
#include "handlers/read.hpp"
#include "handlers/rename.hpp"
#include "handlers/stat.hpp"

extern StorageManager *storage_manager;
extern HomeNodeService *home_node_service;
extern capiocl::engine::Engine *capio_cl_engine;

/*
 * Reads, stats and listings of a file whose home is another server, "peer", or whose home is not
 * known yet. There is one server here: the other one is the recording backend, and the home is
 * what learn() says, as when the peer announces the file.
 */
class RemoteHomeTest : public testing::Test {
  protected:
    static constexpr pid_t TID = 6161;
    static constexpr int FD    = 9;
    static constexpr auto PEER = "peer";
    RecordingBackend recording;
    ScopedTestBackend scoped{&recording};
    TestClient client{TID};
    std::string path;

    void SetUp() override {
        static int run = 0;
        path           = std::string("/remote home/") +
                         testing::UnitTest::GetInstance()->current_test_info()->name() + " " +
                         std::to_string(run++);
    }

    void TearDown() override {
        if (const auto c_file = storage_manager->tryGet(path)) {
            c_file->get().setCommitted(); // releases the threads still waiting on the file
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            storage_manager->removeFromTid(TID, FD);
            storage_manager->remove(path);
        }
        home_node_service->forget(path, PEER);
        home_node_service->forget(path, backend->get_node_name());
    }

    /// @brief A file open by the client at @p offset, holding @p data as far as this server knows
    CapioFile &open_file(const std::string &data, const off64_t offset, const bool directory) {
        CapioFile &c_file =
            directory ? storage_manager->addDirectory(path) : storage_manager->addFile(path, 0);
        c_file.createBuffer(path, false);
        if (!data.empty()) {
            c_file.expandBuffer(static_cast<off64_t>(data.size()));
            std::memcpy(c_file.getBuffer(), data.data(), data.size());
            c_file.insertSector(0, static_cast<off64_t>(data.size()));
        }
        storage_manager->addFileToTid(TID, FD, path, offset);
        return c_file;
    }

    /// @brief The request a read of @p count bytes from @p offset sends to the home
    std::string read_request(const off64_t count, const off64_t offset, const bool getdents) const {
        return "0000 " + std::to_string(TID) + " " + std::to_string(FD) + " " +
               std::to_string(count) + " " + std::to_string(offset) + " " +
               (getdents ? "1 " : "0 ") + path;
    }

    static std::string dirent_named(const char *name) {
        linux_dirent64 entry{};
        entry.d_reclen = sizeof(linux_dirent64);
        std::strcpy(entry.d_name, name);
        return {reinterpret_cast<const char *>(&entry), sizeof(entry)};
    }
};

TEST_F(RemoteHomeTest, AReadOfDataThisServerHasIsServedHere) {
    open_file("0123456789", 2, false);
    home_node_service->learn(path, PEER);
    handle_read(TID, FD, 4);
    EXPECT_EQ(client.reply(), 2 + 4);
    EXPECT_EQ(storage_manager->getFileOffset(TID, FD), 2 + 4);
    EXPECT_EQ(recording.count(), 0);
}

TEST_F(RemoteHomeTest, AReadOfACommittedFileEntirelyHereIsALocalRead) {
    CapioFile &c_file = open_file("0123", 0, false);
    c_file.setRealFileSize(4);
    c_file.setCommitted();
    home_node_service->learn(path, PEER);
    handle_read(TID, FD, 10); // past the end: the local read answers with what there is
    EXPECT_EQ(client.reply(), 4);
    EXPECT_EQ(recording.count(), 0);
}

TEST_F(RemoteHomeTest, AReadOfDataThisServerLacksIsAskedOfTheHome) {
    open_file("01", 0, false);
    home_node_service->learn(path, PEER);
    handle_read(TID, FD, 8);
    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, read_request(8, 0, false));
    EXPECT_EQ(sent->second, PEER);
}

TEST_F(RemoteHomeTest, AReadOfAFileWithNoKnownHomeWaitsForIt) {
    open_file("01", 0, false);
    handle_read(TID, FD, 8);
    EXPECT_EQ(recording.message(0, std::chrono::milliseconds(100)), std::nullopt);
    home_node_service->learn(path, PEER); // as when the peer announces it
    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, read_request(8, 0, false));
}

TEST_F(RemoteHomeTest, AReadOfAFileThatTurnsOutToBeHereIsALocalRead) {
    CapioFile &c_file = open_file("0123", 0, false);
    c_file.setCommitted();
    handle_read(TID, FD, 4);
    EXPECT_EQ(client.reply(std::chrono::milliseconds(100)), std::nullopt);
    home_node_service->setHome(path);
    EXPECT_EQ(client.reply(), 4);
    EXPECT_EQ(recording.count(), 0);
}

TEST_F(RemoteHomeTest, AStatOfACommittedFileOfThePeerIsAnsweredHere) {
    CapioFile &c_file = open_file("0123", 0, false);
    c_file.setCommitted();
    home_node_service->learn(path, PEER);
    reply_stat(TID, path);
    EXPECT_EQ(client.reply(), 4);
    EXPECT_EQ(client.reply(), 0);
    EXPECT_EQ(recording.count(), 0);
}

TEST_F(RemoteHomeTest, AStatOfAFileOfThePeerBeingWrittenIsAskedOfThePeer) {
    open_file("01", 0, false);
    home_node_service->learn(path, PEER);
    reply_stat(TID, path);
    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, "0002 " + std::to_string(TID) + " " + path);
    EXPECT_EQ(sent->second, PEER);
}

TEST_F(RemoteHomeTest, AStatOfAFileWithNoKnownHomeWaitsForIt) {
    open_file("01", 0, false);
    reply_stat(TID, path);
    EXPECT_EQ(client.reply(std::chrono::milliseconds(100)), std::nullopt);
    home_node_service->learn(path, PEER);
    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, "0002 " + std::to_string(TID) + " " + path);
}

TEST_F(RemoteHomeTest, AStatOfAFileThatTurnsOutToBeHereIsAnsweredHere) {
    open_file("012", 0, false);
    reply_stat(TID, path);
    EXPECT_EQ(client.reply(std::chrono::milliseconds(100)), std::nullopt);
    home_node_service->setHome(path);
    EXPECT_EQ(client.reply(), 3);
    EXPECT_EQ(client.reply(), 0);
}

TEST_F(RemoteHomeTest, AStatOfAFileItsProducerHasNotCreatedFails) {
    std::string app_name = client_manager->getAppName(TID);
    capio_cl_engine->addProducer(path, app_name); // the path is new to every test
    reply_stat(TID, path);
    EXPECT_EQ(client.reply(), -1);
    EXPECT_EQ(client.reply(), -1);
}

TEST_F(RemoteHomeTest, AListingOfEntriesThisServerHasIsServedHere) {
    const std::string entries = dirent_named("a") + dirent_named("b");
    open_file(entries, 0, true);
    home_node_service->learn(path, PEER);
    handle_getdents(TID, FD, static_cast<off64_t>(entries.size()));
    EXPECT_EQ(client.reply(), static_cast<off64_t>(entries.size()));
    EXPECT_EQ(recording.count(), 0);
}

TEST_F(RemoteHomeTest, AListingOfACommittedDirectoryEntirelyHereIsServedHere) {
    const std::string entries = dirent_named("a");
    CapioFile &c_file         = open_file(entries, 0, true);
    c_file.setRealFileSize(static_cast<off64_t>(entries.size()));
    c_file.setCommitted();
    home_node_service->learn(path, PEER);
    handle_getdents(TID, FD, 10 * sizeof(linux_dirent64));
    EXPECT_EQ(client.reply(), static_cast<off64_t>(entries.size()));
    EXPECT_EQ(storage_manager->getFileOffset(TID, FD), static_cast<off64_t>(entries.size()));
}

TEST_F(RemoteHomeTest, AListingOfEntriesThisServerLacksIsAskedOfTheHome) {
    open_file("", 0, true);
    home_node_service->learn(path, PEER);
    handle_getdents(TID, FD, sizeof(linux_dirent64));
    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, read_request(sizeof(linux_dirent64), 0, true));
}

TEST_F(RemoteHomeTest, AListingOfADirectoryWithNoKnownHomeWaitsForIt) {
    open_file("", 0, true);
    handle_getdents(TID, FD, sizeof(linux_dirent64));
    EXPECT_EQ(recording.message(0, std::chrono::milliseconds(100)), std::nullopt);
    home_node_service->learn(path, PEER);
    const auto sent = recording.message(0);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->first, read_request(sizeof(linux_dirent64), 0, true));
}

TEST_F(RemoteHomeTest, AListingOfADirectoryThatTurnsOutToBeHereIsServedHere) {
    const std::string entries = dirent_named("a");
    open_file(entries, 0, true);
    handle_getdents(TID, FD, static_cast<off64_t>(entries.size()));
    EXPECT_EQ(client.reply(std::chrono::milliseconds(100)), std::nullopt);
    home_node_service->setHome(path);
    EXPECT_EQ(client.reply(), static_cast<off64_t>(entries.size()));
}

/*
 * The memory variant's rename requests: "tid old_size old_pathnew_path", the new name missing when
 * the file leaves CAPIO_DIR.
 */
class RenameHandlerTest : public RemoteHomeTest {
  protected:
    std::string request(const std::string &old_path, const std::string &new_path) const {
        return std::to_string(TID) + " " + std::to_string(old_path.size()) + " " + old_path +
               new_path;
    }
};

TEST_F(RenameHandlerTest, AFileThatLeavesCapioDirKeepsItsDescriptors) {
    open_file("0123", 0, false);
    home_node_service->setHome(path);
    rename_handler(request(path, "").c_str());
    EXPECT_EQ(client.reply(), 0);
    EXPECT_FALSE(storage_manager->tryGet(path).has_value());
    EXPECT_EQ(home_node_service->getHome(path), std::nullopt);
    // The descriptor still names the file, under a name no virtual path has
    EXPECT_EQ(storage_manager->getPath(TID, FD).native().rfind("left:", 0), 0);
    EXPECT_EQ(storage_manager->get(TID, FD).getStoredSize(), 4);
    storage_manager->remove(storage_manager->getPath(TID, FD)); // closes the descriptor too
}

TEST_F(RenameHandlerTest, ARenameOfAFileTheServerDoesNotHaveFails) {
    rename_handler(request(path, "").c_str());
    EXPECT_EQ(client.reply(), 1);
    rename_handler(request(path, path + " new").c_str());
    EXPECT_EQ(client.reply(), 1);
}

TEST_F(RenameHandlerTest, MalformedRequestsAreIgnored) {
    rename_handler("not a request");
    rename_handler((std::to_string(TID) + " 100 short").c_str()); // the names are cut
    EXPECT_EQ(client.reply(std::chrono::milliseconds(100)), std::nullopt);
}
