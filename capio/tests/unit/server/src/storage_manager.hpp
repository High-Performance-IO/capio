#ifndef CAPIO_STORAGE_MANAGER_TEST_HPP
#define CAPIO_STORAGE_MANAGER_TEST_HPP
#include "storage/manager.hpp"
#include "utils/location.hpp"

TEST(StorageManagerTestSuite, testGetPaths) {
    ClientManager client_manager;
    StorageManager storage_manager(&client_manager);

    std::vector<std::string> test_file_paths = {
        "test1.txt",
        "test2.txt",
        "test3.txt",
    };

    for (const auto &path : test_file_paths) {
        storage_manager.add(path, false, 0);
    }

    const auto storage_paths = storage_manager.getPaths();

    EXPECT_EQ(storage_paths.size(), test_file_paths.size());

    for (const auto &path : test_file_paths) {
        EXPECT_TRUE(std::find(storage_paths.begin(), storage_paths.end(), path) !=
                    storage_paths.end());
    }
}

TEST(StorageManagerTestSuite, testExceptions) {
    ClientManager client_manager;
    StorageManager storage_manager(&client_manager);

    EXPECT_THROW(storage_manager.get("test.txt"), std::runtime_error);
    EXPECT_THROW(storage_manager.get(1234, 1234), std::runtime_error);
}

TEST(StorageManagerTestSuite, testInitDirectory) {
    ClientManager client_manager;
    StorageManager storage_manager(&client_manager);
    node_name = new char[HOST_NAME_MAX];
    gethostname(node_name, HOST_NAME_MAX);
    open_files_location();

    capio_cl_engine.setDirectory("myDirectory");
    capio_cl_engine.setDirectoryFileCount("myDirectory", 10);

    storage_manager.add("myDirectory", true, 0);

    const auto &dir = storage_manager.get("myDirectory");

    EXPECT_EQ(dir.get_buf_size(), CAPIO_DEFAULT_DIR_INITIAL_SIZE);

    storage_manager.updateDirectory(1, "myDirectory");
    const auto &dir1 = storage_manager.get("myDirectory");

    EXPECT_FALSE(dir1.first_write);
}

TEST(StorageManagerTestSuite, testAddDirectoryFailure) {
    char *old_capio_dir = getenv("CAPIO_DIR");
    setenv("CAPIO_DIR", "/", 1);
    node_name = new char[HOST_NAME_MAX];
    gethostname(node_name, HOST_NAME_MAX);
    open_files_location();
    ClientManager client_manager;
    StorageManager storage_manager(&client_manager);
    storage_manager.add("/tmp", true, 0);
    EXPECT_EQ(storage_manager.addDirectory(1, "/tmp/newDirectoryFail"), 0);
    EXPECT_EQ(storage_manager.addDirectory(1, "/tmp/newDirectoryFail"), 1);
    if (old_capio_dir != nullptr) {
        setenv("CAPIO_DIR", old_capio_dir, 1);
    }
}

TEST(StorageManagerTestSuite, testRemameFile) {
    ClientManager client_manager;
    StorageManager storage_manager(&client_manager);

    storage_manager.add("oldName", false, 0);
    storage_manager.add("oldNameNoChange", false, 0);

    storage_manager.addFileToTid(1234, 3, "oldName", 0);
    storage_manager.addFileToTid(1234, 4, "oldNameNoChange", 0);

    storage_manager.add("oldNameNoChange", false, 0);
    storage_manager.rename("oldName", "newName");

    EXPECT_THROW(storage_manager.get("oldName"), std::runtime_error);
    EXPECT_NO_THROW(storage_manager.get("oldNameNoChange"));
}

#endif // CAPIO_STORAGE_MANAGER_TEST_HPP
