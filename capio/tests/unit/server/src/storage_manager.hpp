#ifndef CAPIO_STORAGE_MANAGER_TEST_HPP
#define CAPIO_STORAGE_MANAGER_TEST_HPP
#include "storage/manager.hpp"

TEST(StorageMAnagerTestSuite, testGetPaths) {
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

#endif // CAPIO_STORAGE_MANAGER_TEST_HPP
