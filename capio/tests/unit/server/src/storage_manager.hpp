#ifndef CAPIO_STORAGE_MANAGER_TEST_HPP
#define CAPIO_STORAGE_MANAGER_TEST_HPP

TEST_F(StorageManagerTestEnvironment, testGetPaths) {

    std::vector<std::string> test_file_paths = {
        "test1.txt",
        "test2.txt",
        "test3.txt",
    };

    for (const auto &path : test_file_paths) {
        storage_manager->add(path, false, 0);
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

    storage_manager->add("myDirectory", true, 0);

    const auto &dir = storage_manager->get("myDirectory");

    EXPECT_EQ(dir.get_buf_size(), CAPIO_DEFAULT_DIR_INITIAL_SIZE);

    storage_manager->updateDirectory(1, "myDirectory");
    const auto &dir1 = storage_manager->get("myDirectory");

    EXPECT_FALSE(dir1.first_write);
}

TEST_F(StorageManagerTestEnvironment, testAddDirectoryFailure) {
    char *old_capio_dir = getenv("CAPIO_DIR");
    setenv("CAPIO_DIR", "/", 1);
    open_files_location();

    storage_manager->add("/tmp", true, 0);
    EXPECT_EQ(storage_manager->addDirectory(1, "/tmp/newDirectoryFail"), 0);
    EXPECT_EQ(storage_manager->addDirectory(1, "/tmp/newDirectoryFail"), 1);
    if (old_capio_dir != nullptr) {
        setenv("CAPIO_DIR", old_capio_dir, 1);
    }
}

TEST_F(StorageManagerTestEnvironment, testRemameFile) {

    storage_manager->add("oldName", false, 0);
    storage_manager->add("oldNameNoChange", false, 0);

    storage_manager->addFileToTid(1234, 3, "oldName", 0);
    storage_manager->addFileToTid(1234, 4, "oldNameNoChange", 0);

    storage_manager->add("oldNameNoChange", false, 0);
    storage_manager->rename("oldName", "newName");

    EXPECT_THROW(storage_manager->get("oldName"), std::runtime_error);
    EXPECT_NO_THROW(storage_manager->get("oldNameNoChange"));
}

TEST_F(StorageManagerTestEnvironment, testNumberOfOpensAndCloses) {

    storage_manager->add("myFile", false, 0);
    storage_manager->addFileToTid(1234, 3, "myFile", 0);
    storage_manager->addFileToTid(1234, 4, "myFile", 0);

    EXPECT_FALSE(storage_manager->get("myFile").is_deletable());

    storage_manager->get("myFile").close();
    EXPECT_FALSE(storage_manager->get("myFile").is_deletable());

    storage_manager->get("myFile").close();
    EXPECT_TRUE(storage_manager->get("myFile").is_deletable());
}

TEST_F(StorageManagerTestEnvironment, testNumberOfOpensAfterClone) {

    storage_manager->add("myFile", false, 0);
    storage_manager->addFileToTid(1234, 3, "myFile", 0);
    storage_manager->clone(1234, 5678);

    EXPECT_FALSE(storage_manager->get("myFile").is_deletable());

    storage_manager->removeFromTid(1234, 3);
    EXPECT_FALSE(storage_manager->get("myFile").is_deletable());

    storage_manager->removeFromTid(5678, 3);
    storage_manager->get("myFile").close();

    EXPECT_TRUE(storage_manager->get("myFile").is_deletable());
}

#endif // CAPIO_STORAGE_MANAGER_TEST_HPP
