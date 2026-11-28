#ifndef CAPIO_STORAGE_MANAGER_TEST_HPP
#define CAPIO_STORAGE_MANAGER_TEST_HPP
#include <gtest/gtest.h>

#include <array>
#include <cstdlib>

#include "child.hpp"

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "remote/backend/none.hpp"
#include "remote/home_node_service.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/filesystem.hpp"
#include "utils/runtime_configuration.hpp"

extern StorageManager *storage_manager;
extern capiocl::engine::Engine *capio_cl_engine;
extern Backend *backend;

class StorageManagerTestEnvironment : public testing::Test {
  protected:
    void SetUp() override {
        ASSERT_EQ(backend, nullptr);
        backend = &test_backend;
    }

    void TearDown() override { backend = nullptr; }

  private:
    NoneBackend test_backend{0, nullptr};
};

TEST_F(StorageManagerTestEnvironment, testGetPaths) {

    std::vector<std::string> test_file_paths = {
        "test1.txt",
        "test2.txt",
        "test3.txt",
    };

    for (const auto &path : test_file_paths) {
        storage_manager->addFile(path, 0);
    }

    const auto storage_paths = storage_manager->getPaths();

    EXPECT_EQ(storage_paths.size(), test_file_paths.size());

    for (const auto &path : test_file_paths) {
        EXPECT_TRUE(std::find(storage_paths.begin(), storage_paths.end(), path) !=
                    storage_paths.end());
    }
}

TEST_F(StorageManagerTestEnvironment, testExceptions) {

    EXPECT_THROW(storage_manager->get("test.txt"), std::runtime_error);
    EXPECT_THROW(storage_manager->get(1234, 1234), std::runtime_error);
}

TEST_F(StorageManagerTestEnvironment, testInitDirectory) {

    capio_cl_engine->setDirectory("myDirectory");
    capio_cl_engine->setDirectoryFileCount("myDirectory", 10);

    storage_manager->addDirectory("myDirectory");

    auto &dir = storage_manager->get("myDirectory");

    // Directory buffers are allocated lazily by createBuffer(), so nothing is
    // mapped yet: getBufSize() reflects the real allocation, i.e. 0.
    EXPECT_EQ(dir.getBufSize(), 0);
    dir.createBuffer("", true);
    EXPECT_EQ(dir.getBufSize(), CAPIO_DEFAULT_DIR_INITIAL_SIZE);

    storage_manager->updateDirectory(1, "myDirectory");
    const auto &dir1 = storage_manager->get("myDirectory");

    EXPECT_FALSE(dir1.isFirstWrite());
}

TEST_F(StorageManagerTestEnvironment, testAddDirectoryFailure) {
    storage_manager->addDirectory("/tmp");
    EXPECT_EQ(storage_manager->createDirectory(1, "/tmp/newDirectoryFail"), 0);
    EXPECT_EQ(storage_manager->createDirectory(1, "/tmp/newDirectoryFail"), 1);
}

TEST_F(StorageManagerTestEnvironment, testRemameFile) {

    storage_manager->addFile("oldName", 0);
    storage_manager->addFile("oldNameNoChange", 0);

    storage_manager->addFileToTid(1234, 3, "oldName", 0);
    storage_manager->addFileToTid(1234, 4, "oldNameNoChange", 0);

    storage_manager->addFile("oldNameNoChange", 0);
    storage_manager->rename("oldName", "newName");

    EXPECT_THROW(storage_manager->get("oldName"), std::runtime_error);
    EXPECT_NO_THROW(storage_manager->get("oldNameNoChange"));
}

TEST_F(StorageManagerTestEnvironment, testNumberOfOpensAndCloses) {

    storage_manager->addFile("myFile", 0);
    storage_manager->addFileToTid(1234, 3, "myFile", 0);
    storage_manager->addFileToTid(1234, 4, "myFile", 0);

    EXPECT_FALSE(storage_manager->get("myFile").deletable());

    storage_manager->get("myFile").close();
    EXPECT_FALSE(storage_manager->get("myFile").deletable());

    storage_manager->get("myFile").close();
    EXPECT_TRUE(storage_manager->get("myFile").deletable());
}

TEST_F(StorageManagerTestEnvironment, testNumberOfOpensAfterClone) {

    storage_manager->addFile("myFile", 0);
    storage_manager->addFileToTid(1234, 3, "myFile", 0);
    storage_manager->clone(1234, 5678);

    EXPECT_FALSE(storage_manager->get("myFile").deletable());

    storage_manager->removeFromTid(1234, 3);
    EXPECT_FALSE(storage_manager->get("myFile").deletable());

    storage_manager->removeFromTid(5678, 3);
    storage_manager->get("myFile").close();

    EXPECT_TRUE(storage_manager->get("myFile").deletable());
}

TEST_F(StorageManagerTestEnvironment, testVirtualRootIsLocal) {
    // "/" is the CAPIO_DIR of the clients: local, and where the registration of parents stops
    storage_manager->createDirectory(1, "/");
    EXPECT_TRUE(storage_manager->get("/").isDirectory());
    EXPECT_EQ(home_node_service->getHome("/"), backend->get_node_name());

    EXPECT_EQ(get_parent_dir_path("/f"), "/");
    EXPECT_EQ(get_parent_dir_path("/d/f"), "/d");
    EXPECT_EQ(storage_manager->createDirectory(1, "/virtual_sub"), 0);
    EXPECT_EQ(home_node_service->getHome("/virtual_sub"), backend->get_node_name());
    EXPECT_TRUE(storage_manager->get("/").isDirectory());

    storage_manager->remove("/virtual_sub");
}

TEST_F(StorageManagerTestEnvironment, addFileAndAddDirectoryMakeTheKindTheyAreNamedAfter) {
    CapioFile &file = storage_manager->addFile("/kind_file", 0);
    EXPECT_FALSE(file.isDirectory());
    // Added once: the second call finds the same object
    EXPECT_EQ(&storage_manager->addFile("/kind_file", 64), &file);
    EXPECT_EQ(&storage_manager->addFile("/kind_file"), &file);

    CapioFile &directory = storage_manager->addDirectory("/kind_dir");
    EXPECT_TRUE(directory.isDirectory());
    EXPECT_EQ(&storage_manager->addDirectory("/kind_dir"), &directory);
    // Unlike createDirectory, no "." and ".." entries, and no entry in the parent
    EXPECT_TRUE(directory.isFirstWrite());
    EXPECT_EQ(directory.getCurrentDirectoryFileCount(), 0U);
    directory.createBuffer("/kind_dir", false);
    EXPECT_EQ(directory.getBufSize(), CAPIO_DEFAULT_DIR_INITIAL_SIZE);

    EXPECT_EQ(storage_manager->createDirectory(1, "/kind_created"), 0);
    const CapioFile &created = storage_manager->get("/kind_created");
    EXPECT_TRUE(created.isDirectory());
    EXPECT_FALSE(created.isFirstWrite());
    EXPECT_EQ(created.getCurrentDirectoryFileCount(), 2U); // "." and ".."

    for (const auto *path : {"/kind_file", "/kind_dir", "/kind_created"}) {
        storage_manager->remove(path);
    }
}

TEST_F(StorageManagerTestEnvironment, aNewFileHasTheInitialSizeOfTheConfiguration) {
    // CAPIO_FILE_INIT_SIZE is read with the configuration, so in a child that sets its own (#326)
    EXPECT_EQ(run_in_child([] {
                  setenv("CAPIO_FILE_INIT_SIZE", "4096", 1);
                  std::array<char *, 2> argv{const_cast<char *>("capio_server"),
                                             const_cast<char *>("--no-config")};
                  RuntimeConfiguration::set(RuntimeConfiguration(argv.size(), argv.data()));

                  EXPECT_EQ(StorageManager::initialFileSize("/sized"), 4096U);
                  CapioFile &file = storage_manager->addFile("/sized");
                  file.createBuffer("/sized", false);
                  EXPECT_EQ(file.getBufSize(), 4096);

                  // An explicit size wins
                  CapioFile &explicit_size = storage_manager->addFile("/sized_explicitly", 16);
                  explicit_size.createBuffer("/sized_explicitly", false);
                  EXPECT_EQ(explicit_size.getBufSize(), 16);
              }),
              0);
    EXPECT_EQ(StorageManager::initialFileSize("/sized"),
              static_cast<size_t>(RuntimeConfiguration::get().getFileInitialSize()));
}

#endif // CAPIO_STORAGE_MANAGER_TEST_HPP
