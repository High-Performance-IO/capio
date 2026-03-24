#include "storage/capio_file.hpp"
#include "common/dirent.hpp"
#include "common/env.hpp"
#include "remote/backend.hpp"
#include "storage/manager.hpp"
#include "utils/location.hpp"

extern StorageManager *storage_manager;
extern Backend *backend;

#include <gtest/gtest.h>
#include <thread>

TEST(ServerTest, TestInsertSingleSector) {
    CapioFile c_file;
    c_file.insertSector(1, 3);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 3L}), sectors.end());
}

TEST(ServerTest, TestBufferAllocation) {
    CapioFile c_file;
    EXPECT_TRUE(c_file.bufferToAllocate());
    c_file.createBuffer("test.dat", true);
    EXPECT_FALSE(c_file.bufferToAllocate());
    EXPECT_EQ(c_file.getBufSize(), 0);
    EXPECT_EQ(c_file.getRealFileSize(), 0);
    EXPECT_NE(nullptr, c_file.getBuffer());
}

TEST(ServerTest, TestInsertTwoNonOverlappingSectors) {
    CapioFile c_file;
    c_file.insertSector(5, 7);
    c_file.insertSector(1, 3);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 2);
    auto it = sectors.begin();
    EXPECT_EQ(std::make_pair(1L, 3L), *it);
    std::advance(it, 1);
    EXPECT_EQ(std::make_pair(5L, 7L), *it);
}

TEST(ServerTest, TestInsertTwoOverlappingSectors) {
    CapioFile c_file;
    c_file.insertSector(2, 4);
    c_file.insertSector(1, 3);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsSameStart) {
    CapioFile c_file;
    c_file.insertSector(1, 4);
    c_file.insertSector(1, 3);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsSameEnd) {
    CapioFile c_file;
    c_file.insertSector(1, 4);
    c_file.insertSector(2, 4);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsNested) {
    CapioFile c_file;
    c_file.insertSector(1, 4);
    c_file.insertSector(2, 3);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestDestructionOfPermanentCapioFile) {
    auto *c_file = new CapioFile(false, true, 1000, 1);
    c_file->createBuffer("test.dat", true);
    delete c_file;
    EXPECT_TRUE(std::filesystem::exists("test.dat"));
    std::filesystem::remove("test.dat");
}

TEST(ServerTest, TestDestructionOfPermanentCapioFileDirectory) {
    auto *c_file = new CapioFile(true, true, 1000, 1);
    c_file->createBuffer("mydirectory", true);
    delete c_file;
    EXPECT_TRUE(std::filesystem::exists("mydirectory"));
    EXPECT_TRUE(std::filesystem::is_directory("mydirectory"));
    std::filesystem::remove("mydirectory");
}

TEST(ServerTest, TestCapioFileWaitForDataMultithreaded) {
    CapioFile file;

    SPSCQueue queue("test_queue", get_cache_lines(), get_cache_line_size(), "test_wf");

    std::mutex _lock;
    _lock.lock();

    std::thread t([&_lock, &file, &queue] {
        _lock.lock();
        file.expandBuffer(1000);
        file.registerFirstWrite();

        EXPECT_NE(file.getBuffer(), nullptr);
        char buffer[1000];
        for (std::size_t i = 0; i < 1000; ++i) {
            buffer[i] = 33 + (i % 93);
        }

        queue.write(buffer, 1000);
        file.insertSector(0, 1000);
        file.readFromQueue(queue, 0, 1000);
    });

    _lock.unlock();
    file.waitForData(1000);

    auto stored_size = file.getFileSize();
    EXPECT_EQ(stored_size, 1000);

    const auto buf = file.getBuffer();
    EXPECT_NE(buf, nullptr);
    for (std::size_t i = 0; i < 1000; ++i) {
        EXPECT_EQ(buf[i], 33 + (i % 93));
    }

    t.join();
}

TEST(ServerTest, TestCapioFileWaitForDataMultithreadedWithCommit) {
    CapioFile file;

    SPSCQueue queue("test_queue", get_cache_lines(), get_cache_line_size(), "test_wf");

    std::mutex _lock;
    _lock.lock();

    std::thread t([&_lock, &file, &queue] {
        _lock.lock();
        file.setCommitted();
    });

    _lock.unlock();
    file.waitForData(1000);

    EXPECT_EQ(file.getFileSize(), 0);

    t.join();
}

TEST(ServerTest, TestCapioFileWaitForCompletion) {
    CapioFile file;

    std::mutex _lock;
    _lock.lock();

    std::thread t([&] {
        _lock.lock();
        file.expandBuffer(1000);
        file.registerFirstWrite();

        EXPECT_NE(file.getBuffer(), nullptr);
        char buffer[1000];
        for (std::size_t i = 0; i < 1000; ++i) {
            buffer[i] = 33 + (i % 93);
        }

        memcpy(file.getBuffer(), buffer, 1000);

        file.insertSector(0, 1000);
        file.setCommitted();
    });

    _lock.unlock();
    file.waitForCommit();

    auto stored_size = file.getFileSize();
    EXPECT_EQ(stored_size, 1000);

    const auto buf = file.getBuffer();
    EXPECT_NE(buf, nullptr);
    for (std::size_t i = 0; i < 1000; ++i) {
        EXPECT_EQ(buf[i], 33 + (i % 93));
    }

    t.join();
}

TEST(ServerTest, TestCommitCapioFile) {
    auto file = new CapioFile(false, true, 1000, 1);
    file->createBuffer("test.dat", true);
    EXPECT_EQ(std::filesystem::file_size("test.dat"), 1000);
    file->close();
    file->dump();
    EXPECT_EQ(std::filesystem::file_size("test.dat"), 0);
    delete file;
    EXPECT_TRUE(std::filesystem::exists("test.dat"));
    std::filesystem::remove("test.dat");
}

TEST(ServerTest, TestDumpNotHomeNode) {
    auto file = new CapioFile(false, true, 1000, 1);
    file->createBuffer("test.dat", false);
    file->close();
    file->dump();
    delete file;
    EXPECT_FALSE(std::filesystem::exists("test.dat"));
}

TEST(ServerTest, TestCommitAndDeleteDirectory) {
    EXPECT_FALSE(std::filesystem::exists("mydir"));
    auto file = new CapioFile(true, true, 1000, 1);
    file->createBuffer("mydir", true);
    EXPECT_TRUE(std::filesystem::exists("mydir"));
    EXPECT_TRUE(std::filesystem::is_directory("mydir"));
    delete file;
    EXPECT_TRUE(std::filesystem::exists("mydir"));
    std::filesystem::remove("mydir");
}

TEST(ServerTest, TesMemcpyCapioFile) {
    CapioFile file;

    file.createBuffer("test.dat", true);

    // NOTE: here we only simulate a write operation on the file, without actually writing to _buf
    file.insertSector(0, 100);
    file.insertSector(100, 200);

    file.expandBuffer(2000);

    EXPECT_EQ(file.getStoredSize(), 200);
    EXPECT_EQ(file.getBufSize(), 2000);
}

TEST(ServerTest, TestCloseCapioFile) {
    CapioFile file(false, false, 0, 0);
    EXPECT_TRUE(file.closed()); // TEST for n_close_expected == -1

    CapioFile file1(false, false, 0, 10);
    EXPECT_FALSE(file1.closed());
    for (std::size_t i = 0; i < 10; ++i) {
        file1.open();
        EXPECT_FALSE(file1.closed());
        file1.close();
    }
    EXPECT_TRUE(file1.closed());
}

TEST(ServerTest, TestCapioFileSeekData) {
    CapioFile file;

    EXPECT_EQ(file.seekData(100), -1);
    EXPECT_EQ(file.seekData(0), 0);

    file.insertSector(0, 1000);
    EXPECT_EQ(file.seekData(100), 100);
    EXPECT_EQ(file.seekData(2000), -1);
    file.insertSector(2000, 3000);
    EXPECT_NE(file.seekData(1500), 1500); // return here the closest offset...

    CapioFile file1;
    file1.insertSector(200, 300);
    EXPECT_EQ(file1.seekData(1), 200);
}

TEST(ServerTest, TestCapioFileSeekHole) {
    CapioFile file;

    EXPECT_EQ(file.seekHole(100), -1);
    EXPECT_EQ(file.seekHole(0), 0);
    file.insertSector(0, 1000);
    EXPECT_EQ(file.seekHole(100), 1000);
    EXPECT_EQ(file.seekHole(2000), -1);
    file.insertSector(2000, 3000);
    EXPECT_EQ(file.seekHole(1500), 1500); // return here the closest offset...

    CapioFile file1;
    file1.insertSector(200, 300);
    EXPECT_EQ(file1.seekHole(1), 1);
}

TEST(ServerTest, TestAddAndRemoveFD) {
    CapioFile file;
    file.addFd(12345, 4);
    file.addFd(12345, 5);

    file.removeFd(12345, 6);
    EXPECT_EQ(file.getFds().size(), 2);
    file.removeFd(12345, 5);
    EXPECT_EQ(file.getFds().size(), 1);
    file.removeFd(12345, 4);
    EXPECT_EQ(file.getFds().size(), 0);
}

TEST(ServerTest, TestSetGetRealFileSize) {
    CapioFile file;
    EXPECT_EQ(file.getRealFileSize(), 0);
    file.setRealFileSize(1234);
    EXPECT_EQ(file.getRealFileSize(), 1234);
}

TEST(ServerTest, TestDeletePermanentDirectory) {
    const auto file = new CapioFile(true, true, 1000, 1);
    file->createBuffer("testDir", true);
    delete file;
    EXPECT_TRUE(std::filesystem::exists("testDir"));
    EXPECT_TRUE(std::filesystem::is_directory("testDir"));
    std::filesystem::remove("testDir");
}

TEST(ServerTest, TestFileSetCommitToFalse) {
    CapioFile file;
    file.setCommitted();
    EXPECT_TRUE(file.isCommitted());
    file.setCommitted(false);
    EXPECT_FALSE(file.isCommitted());
}

class MockBackend : public Backend {
  public:
    MockBackend() : Backend(HOST_NAME_MAX) {}

    void recv_file(char *shm, const std::string &source, const long int bytes_expected) override {
        for (std::size_t i = 0; i < bytes_expected; ++i) {
            shm[i] = 33 + (i % 93);
        }
    }

    const std::set<std::string> get_nodes() override { return {node_name}; }
    void handshake_servers() override {}
    RemoteRequest read_next_request() override { return {nullptr, ""}; }
    void send_file(char *shm, long int nbytes, const std::string &target) override {}
    void send_request(const char *message, int message_len, const std::string &target) override {}
};

class MockBackendTestFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        backend = new MockBackend();
        open_files_location();
    }

    void TearDown() override { delete backend; }
};

TEST_F(MockBackendTestFixture, TestReadFromNodeMockBackend) {

    CapioFile file1;
    file1.createBuffer("testDir", true);
    file1.expandBuffer(1000);
    file1.registerFirstWrite();
    file1.insertSector(0, 1000);

    file1.readFromNode("none", 0, 1000);

    const auto buf = file1.getBuffer();
    for (std::size_t i = 0; i < 1000; ++i) {
        EXPECT_EQ(buf[i], 33 + (i % 93));
    }
}

TEST(ServerTest, TestGetSectorEnd) {
    CapioFile file;

    EXPECT_EQ(file.getSectorEnd(1234), -1);

    file.insertSector(0, 1234);

    EXPECT_EQ(file.getSectorEnd(120), 1234);
    EXPECT_EQ(file.getSectorEnd(12000), -1);
}

TEST_F(MockBackendTestFixture, TestSimulateDirectoryStreaming) {

    constexpr int NUM_FILES_EXPECTED = 10;

    const std::filesystem::path CAPIO_DIR        = "/tmp";
    const std::filesystem::path stream_directory = CAPIO_DIR / "my_streaming_directory";

    setenv("CAPIO_DIR", CAPIO_DIR.c_str(), 1);
    storage_manager->addDirectory(1234, CAPIO_DIR);
    storage_manager->addDirectory(1234, stream_directory);

    std::mutex mutex_continue;

    std::thread t([&] {
        for (auto i = 0; i < NUM_FILES_EXPECTED; ++i) {
            mutex_continue.lock();
            const std::string filename = "file." + std::to_string(i);
            storage_manager->updateDirectory(1234, stream_directory / filename);
        }
    });

    const auto &file = storage_manager->get(stream_directory);

    long current_offset = 0;

    linux_dirent64 dirent{};

    file.waitForData(current_offset + sizeof(linux_dirent64));
    memcpy(&dirent, file.getBuffer() + current_offset, sizeof(linux_dirent64));
    current_offset += sizeof(linux_dirent64);
    EXPECT_EQ(strcmp(dirent.d_name, "."), 0);
    bzero(&dirent, sizeof(linux_dirent64));

    file.waitForData(current_offset + sizeof(linux_dirent64));
    memcpy(&dirent, file.getBuffer() + current_offset, sizeof(linux_dirent64));
    current_offset += sizeof(linux_dirent64);
    EXPECT_EQ(strcmp(dirent.d_name, ".."), 0);
    bzero(&dirent, sizeof(linux_dirent64));

    for (auto i = 0; i < NUM_FILES_EXPECTED; ++i) {

        file.waitForData(current_offset + sizeof(linux_dirent64));
        memcpy(&dirent, file.getBuffer() + current_offset, sizeof(linux_dirent64));
        const std::string expected_filename = "file." + std::to_string(i);
        EXPECT_EQ(strcmp(dirent.d_name, expected_filename.c_str()), 0);
        bzero(&dirent, sizeof(linux_dirent64));
        mutex_continue.unlock();
        current_offset += sizeof(linux_dirent64);
    }

    t.join();

    storage_manager->remove(stream_directory);
    storage_manager->remove(CAPIO_DIR);
}