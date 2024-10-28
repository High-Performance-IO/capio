#include <gtest/gtest.h>

#include "../server/file-manager/CapioMemoryFile.hpp"

constexpr size_t FILE_SIZE = 8 * 1024 * 1024;

TEST(CapioMemoryFileTest, TestWriteAndRead) {
    CapioMemoryFile file("test.txt");

    // 8 MB buffer
    const auto buffer = new std::vector<char>();
    buffer->reserve(FILE_SIZE);
    for (ssize_t i = 0; i < buffer->size(); i++) {
        buffer->push_back(static_cast<char>('a' + i % 26));
    }
    std::hash<std::string_view> hash_f;
    auto computed_hash_origin = hash_f(std::string_view(buffer->data(), buffer->size()));

    file.writeData(0, buffer->data(), buffer->size());

    auto buffer_read = new std::vector<char>();
    buffer_read->reserve(FILE_SIZE);
    file.readData(0, buffer_read->data(), buffer_read->size());
    const auto computed_read_hash =
        hash_f(std::string_view(buffer_read->data(), buffer_read->size()));

    EXPECT_EQ(computed_hash_origin, computed_read_hash);

    delete buffer;
    delete buffer_read;
}

TEST(CapioMemoryFileTest, TestWriteAndReadDifferentOffset) {
    CapioMemoryFile file("test1.txt");

    // 8 MB buffer
    const auto buffer = new std::vector<char>();
    buffer->reserve(FILE_SIZE);
    for (ssize_t i = 0; i < buffer->size(); i++) {
        buffer->push_back(static_cast<char>('a' + i % 26));
    }
    std::hash<std::string_view> hash_f;
    auto computed_hash_origin = hash_f(std::string_view(buffer->data(), buffer->size()));

    file.writeData(FILE_SIZE / 2, buffer->data(), buffer->size());

    auto buffer_read = new std::vector<char>();
    buffer_read->reserve(FILE_SIZE);
    file.readData(FILE_SIZE / 2, buffer_read->data(), buffer_read->size());
    const auto computed_read_hash =
        hash_f(std::string_view(buffer_read->data(), buffer_read->size()));

    EXPECT_EQ(computed_hash_origin, computed_read_hash);

    delete buffer;
    delete buffer_read;
}

TEST(CapioMemoryFileTest, TestWriteAndReadDifferentOffsetVeryLargeFile) {
    CapioMemoryFile file("test2.txt");

    // 8 MB buffer
    const auto buffer = new std::vector<char>();
    buffer->reserve(10 * FILE_SIZE);
    for (ssize_t i = 0; i < buffer->size(); i++) {
        buffer->push_back(static_cast<char>('a' + i % 26));
    }
    std::hash<std::string_view> hash_f;
    auto computed_hash_origin = hash_f(std::string_view(buffer->data(), buffer->size()));

    file.writeData(FILE_SIZE / 2, buffer->data(), buffer->size());

    auto buffer_read = new std::vector<char>();
    buffer_read->reserve(10 * FILE_SIZE);
    file.readData(FILE_SIZE / 2, buffer_read->data(), buffer_read->size());
    const auto computed_read_hash =
        hash_f(std::string_view(buffer_read->data(), buffer_read->size()));

    EXPECT_EQ(computed_hash_origin, computed_read_hash);

    delete buffer;
    delete buffer_read;
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
