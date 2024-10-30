#include <gtest/gtest.h>
#include <thread>

std::string node_name;

#include "../server/storage-service/CapioFile/CapioMemoryFile.hpp"

constexpr size_t FILE_SIZE = 8 * 1024 * 1024;

TEST(CapioMemoryFileTest, TestSimpleWriteAndReadDifferentOffsets) {
    CapioMemoryFile file("test.txt");
    const std::string string1 = "Hello world string 1";
    const std::string string2 = "Hello world string 2";
    const std::string string3 = "Cantami diva del pelide Achille l'ira funesta che infiniti...";

    char input_buffer1[100]{};
    file.writeData(string1.data(), 0, string1.size());
    file.readData(input_buffer1, 0, string1.size());
    EXPECT_TRUE(input_buffer1 == string1);

    char input_buffer2[100]{};
    file.writeData(string2.data(), FILE_SIZE, string2.size());
    file.readData(input_buffer2, FILE_SIZE, string2.size());
    EXPECT_TRUE(input_buffer2 == string2);

    char input_buffer3[100]{};
    file.writeData(string3.data(), 2 * FILE_SIZE, string3.size());
    file.readData(input_buffer3, 2 * FILE_SIZE, string3.size());
    EXPECT_TRUE(input_buffer3 == string3);
}

TEST(CapioMemoryFileTest, TestHugeFileOnMultiplePages) {
    CapioMemoryFile file1("test.txt");
    capio_off64_t LONG_TEXT_SIZE = 1024 * 1024 * 16;
    auto LONG_TEXT               = new char[LONG_TEXT_SIZE]{}; // 16 MB string
    for (capio_off64_t i = 0; i < LONG_TEXT_SIZE; i++) {
        LONG_TEXT[i] = 'A' + (random() % 26);
    }

    auto input_buffer4     = new char[LONG_TEXT_SIZE + 1]{};
    bool write_count_match = file1.writeData(LONG_TEXT, 0, LONG_TEXT_SIZE) == LONG_TEXT_SIZE;
    EXPECT_TRUE(write_count_match);

    file1.readData(input_buffer4, 0, LONG_TEXT_SIZE);

    bool ok = true;
    for (capio_off64_t i = 0; i < LONG_TEXT_SIZE && ok; i++) {
        ok &= (LONG_TEXT[i] == input_buffer4[i]);
        if (!ok) {
            std::cout << "Check failed at byte " << i << " out of " << LONG_TEXT_SIZE << std::endl;
        }
    }
    EXPECT_TRUE(ok);

    delete LONG_TEXT;
}

/*
 * WARNING: This test uses files that are too ig to be handled by the CI/CD on github,
 * and as such it has been disabled. Nevertheless this test can be executed locally
 *
TEST(CapioMemoryFileTest, TestHugeFileOnMultiplePagesThatStartOnDifferentOffset) {
    CapioMemoryFile file1("test.txt");
    capio_off64_t LONG_TEXT_SIZE = 1024 * 1024 * 16;
    auto LONG_TEXT               = new char[LONG_TEXT_SIZE]{}; // 16 MB string
    for (capio_off64_t i = 0; i < LONG_TEXT_SIZE; i++) {
        LONG_TEXT[i] = 'A' + (random() % 26);
    }

    auto input_buffer4     = new char[LONG_TEXT_SIZE + 1]{};
    bool write_count_match = file1.writeData(LONG_TEXT, 2048, LONG_TEXT_SIZE) == LONG_TEXT_SIZE;
    EXPECT_TRUE(write_count_match);

    file1.readData(input_buffer4, 2048, LONG_TEXT_SIZE);

    bool ok = true;
    for (capio_off64_t i = 0; i < LONG_TEXT_SIZE && ok; i++) {
        ok &= (LONG_TEXT[i] == input_buffer4[i]);
        if (!ok) {
            std::cout << "Check failed at byte " << i << " out of " << LONG_TEXT_SIZE << std::endl;
        }
    }
    EXPECT_TRUE(ok);

    delete LONG_TEXT;
}
*/

TEST(CapioMemoryFileTest, TestWriteAndRead) {
    CapioMemoryFile file("test.txt");

    // 8 MB buffer
    const auto buffer = new std::vector<char>();
    buffer->reserve(FILE_SIZE);
    for (ssize_t i = 0; i < buffer->size(); i++) {
        buffer->push_back(static_cast<char>('a' + i % 26));
    }

    file.writeData(buffer->data(), 0, buffer->size());

    auto buffer_read = new char[FILE_SIZE + 1]{};
    file.readData(buffer_read, 0, FILE_SIZE);

    bool ok = true;
    for (capio_off64_t i = 0; i < buffer->size() && ok; i++) {
        ok &= (buffer->data()[i] == buffer_read[i]) && (buffer->data()[i] != '\0');
        if (!ok) {
            std::cout << "Check failed at byte " << i << " out of " << buffer->size() << std::endl;
        }
    }
    EXPECT_TRUE(ok);

    delete buffer;
    delete buffer_read;
}

TEST(CapioMemoryFileTest, TestWriteAndReadDifferentPageStartOffset) {
    CapioMemoryFile file("test.txt");

    // 8 MB buffer
    const auto buffer = new std::vector<char>();
    buffer->reserve(FILE_SIZE);
    for (ssize_t i = 0; i < buffer->size(); i++) {
        buffer->push_back(static_cast<char>('a' + i % 26));
    }

    file.writeData(buffer->data(), FILE_SIZE / 2, buffer->size());

    auto buffer_read = new char[FILE_SIZE + 1];
    file.readData(buffer_read, FILE_SIZE / 2, FILE_SIZE);

    bool ok = true;
    for (capio_off64_t i = 0; i < buffer->size() && ok; i++) {
        ok &= (buffer->data()[i] == buffer_read[i]) && (buffer->data()[i] != '\0');
        if (!ok) {
            std::cout << "Check failed at byte " << i << " out of " << buffer->size() << std::endl;
        }
    }
    EXPECT_TRUE(ok);

    delete buffer;
    delete buffer_read;
}

TEST(CapioMemoryFileTest, TestThreadsSpscqueueAndCapioMemFile) {

    SPSCQueue communication_queue("test.queue", CAPIO_CACHE_LINES_DEFAULT,
                                  CAPIO_CACHE_LINE_SIZE_DEFAULT, "demo", true);
    CapioMemoryFile file_source("source.txt"), file_destination("destination.txt");

    // 8 MB buffer
    auto buffer = new char[10 * FILE_SIZE];
    for (ssize_t i = 0; i < 10 * FILE_SIZE; i++) {
        buffer[i] = 'a' + i % 26;
    }

    file_source.writeData(buffer, FILE_SIZE / 2, 10 * FILE_SIZE);

    std::thread writer([&communication_queue, &file_source]() {
        file_source.writeToQueue(communication_queue, FILE_SIZE / 2, 10 * FILE_SIZE);
    });

    file_destination.readFromQueue(communication_queue, FILE_SIZE / 2, 10 * FILE_SIZE);
    writer.join();

    auto buffer_read = new char[10 * FILE_SIZE];
    file_destination.readData(buffer_read, FILE_SIZE / 2, 10 * FILE_SIZE);

    bool ok = true;
    for (capio_off64_t i = 0; i < 10 * FILE_SIZE && ok; i++) {
        ok &= (buffer[i] == buffer_read[i]) && (buffer_read[i] != '\0');
        if (!ok) {
            std::cout << "Check failed at byte " << i << " out of " << 10 * FILE_SIZE << std::endl;
        }
    }
    EXPECT_TRUE(ok);

    delete buffer;
    delete buffer_read;
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
