#ifndef CAPIO_SERVER_DIRECTORY_TEST_HPP
#define CAPIO_SERVER_DIRECTORY_TEST_HPP
#include <gtest/gtest.h>

#include <string>

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "common/requests.hpp"
#include "remote/backend/none.hpp"
#include "remote/handlers/directory.hpp"
#include "remote/home_node_service.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"

#include "handlers/stat.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;
extern Backend *backend;
extern HomeNodeService *home_node_service;

/*
 * A directory made through one server, its home, and a file created in it through another. The
 * other server must not abort ("not found in local storage") nor keep a listing of its own: it
 * keeps a stand-in that points to the home, and the entry goes to the home's listing.
 */
class RemoteDirectoryTest : public testing::Test {
  protected:
    static constexpr pid_t TID = 6161;
    const std::string other_node = "another-node";
    std::filesystem::path dir;

    void SetUp() override {
        ASSERT_EQ(backend, nullptr);
        backend = &test_backend;
        static int run = 0;
        dir = std::string("remote_directory_") +
              testing::UnitTest::GetInstance()->current_test_info()->name() + "_" +
              std::to_string(run++);
        client_manager->registerClient(TID, "writer");
    }

    void TearDown() override {
        if (storage_manager->tryGet(dir)) { // the stat test makes no entry for the directory
            storage_manager->remove(dir);
        }
        client_manager->removeClient(TID);
        backend = nullptr;
    }

  private:
    NoneBackend test_backend{0, nullptr};
};

TEST_F(RemoteDirectoryTest, fileInADirectoryOfAnotherServerLeavesAStandInPointingToItsHome) {
    home_node_service->learn(dir, other_node);

    storage_manager->updateDirectory(TID, dir / "file");

    const auto stand_in = storage_manager->tryGet(dir);
    ASSERT_TRUE(stand_in) << "no stand-in for the directory of another server";
    EXPECT_EQ(stand_in->get().getCurrentDirectoryFileCount(), 0u)
        << "the entry was added here, not at the directory's home";
    EXPECT_EQ(home_node_service->getHome(dir), other_node) << "the stand-in took over the home";

    // A second file in the same directory goes to the home as well.
    storage_manager->updateDirectory(TID, dir / "second");
    EXPECT_EQ(stand_in->get().getCurrentDirectoryFileCount(), 0u);
}

TEST_F(RemoteDirectoryTest, homeAddsAndRemovesTheEntriesOfAnotherServer) {
    storage_manager->add(dir, true, CAPIO_DEFAULT_DIR_INITIAL_SIZE);
    home_node_service->learn(dir, backend->get_node_name());

    const std::string file = (dir / "file").native();
    remote_directory_entry_add_handler(RemoteRequest(
        std::to_string(CAPIO_SERVER_REQUEST_DIRECTORY_ENTRY_ADD).insert(0, 2, '0') + " " + file,
        other_node));
    EXPECT_EQ(storage_manager->get(dir).getCurrentDirectoryFileCount(), 1u);

    remote_directory_entry_remove_handler(RemoteRequest(
        std::to_string(CAPIO_SERVER_REQUEST_DIRECTORY_ENTRY_REMOVE).insert(0, 2, '0') + " " +
            file,
        other_node));
    EXPECT_EQ(storage_manager->get(dir).getCurrentDirectoryFileCount(), 0u);
}

/*
 * A stat of a file that does not exist yet waits for it to be created. Created through another
 * server, it has no entry on this one: the stat must go to its home, not abort this server.
 */
TEST_F(RemoteDirectoryTest, statWaitingForAFileCreatedThroughAnotherServerAsksItsHome) {
    const auto file = dir / "created_elsewhere.fits";
    home_node_service->learn(file, other_node);

    wait_for_file_completion(TID, file); // used to abort: "not found in local storage"

    EXPECT_FALSE(storage_manager->tryGet(file)) << "the stat created a local entry for the file";
}

#endif // CAPIO_SERVER_DIRECTORY_TEST_HPP
