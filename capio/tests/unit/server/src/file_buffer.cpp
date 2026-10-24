#include "storage/file_buffer.hpp"

#include <sys/stat.h>

#include <cstring>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {
constexpr off64_t KB = 1024;
constexpr off64_t MB = 1024 * KB;

std::filesystem::path tempFile(const char *suffix) {
    return std::filesystem::temp_directory_path() / ("capio_filebuf_" + std::string(suffix) + ".dat");
}
} // namespace

using HeapBuf   = FileBuffer<HeapBacking>;
using MappedBuf = FileBuffer<MappedBacking>;

// ---- HeapBacking ---------------------------------------------------------

TEST(HeapBackingTest, StartsEmpty) {
    HeapBuf buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.data(), nullptr);
    EXPECT_EQ(buf.size(), 0);
}

TEST(HeapBackingTest, AllocateAndRelease) {
    HeapBuf buf;
    EXPECT_TRUE(buf.allocate(1 * MB));
    EXPECT_FALSE(buf.empty());
    ASSERT_NE(buf.data(), nullptr);
    EXPECT_EQ(buf.size(), 1 * MB);

    // The buffer is real, writable heap memory.
    std::memset(buf.data(), 0xAB, buf.size());

    buf.release();
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.data(), nullptr);
    EXPECT_EQ(buf.size(), 0);
}

TEST(HeapBackingTest, AllocateTwiceReturnsFalse) {
    HeapBuf buf;
    ASSERT_TRUE(buf.allocate(1 * KB));
    EXPECT_FALSE(buf.allocate(1 * MB));
    EXPECT_EQ(buf.size(), 1 * KB);
}

TEST(HeapBackingTest, GrowKeepsData) {
    HeapBuf buf;
    ASSERT_TRUE(buf.allocate(1 * KB));
    for (off64_t i = 0; i < buf.size(); ++i) {
        buf.data()[i] = static_cast<char>(i & 0xFF);
    }

    char *const nb = buf.grow(4 * KB, {{0, 1 * KB}});
    EXPECT_EQ(nb, buf.data());
    EXPECT_GE(buf.size(), 4 * KB);

    // Data written before the grow must survive at the same offsets.
    for (off64_t i = 0; i < 1 * KB; ++i) {
        EXPECT_EQ(buf.data()[i], static_cast<char>(i & 0xFF));
    }
}

TEST(HeapBackingTest, ReleaseIsSafeTwice) {
    HeapBuf buf;
    ASSERT_TRUE(buf.allocate(1 * KB));
    buf.release();
    buf.release(); // double-free must not crash
    buf.release();
}

// ---- MappedBacking -------------------------------------------------------

TEST(MappedBackingTest, AllocateCreatesBackingFile) {
    const auto path = tempFile("mapped_creates");
    std::filesystem::remove(path);
    {
        MappedBuf buf;
        ASSERT_TRUE(buf.allocate(path, 1 * MB));
        EXPECT_FALSE(buf.empty());
        ASSERT_NE(buf.data(), nullptr);
        EXPECT_EQ(buf.size(), 1 * MB);
        EXPECT_EQ(std::filesystem::file_size(path), 1 * MB);

        std::memset(buf.data(), 0x5A, buf.size()); // MAP_SHARED: writable
    }
    std::filesystem::remove(path);
}

TEST(MappedBackingTest, CommitTruncatesAndPersists) {
    const auto path        = tempFile("mapped_commit");
    constexpr off64_t keep = 16;
    std::filesystem::remove(path);
    {
        MappedBuf buf;
        ASSERT_TRUE(buf.allocate(path, 1 * MB));

        std::memset(buf.data(), 'A', keep);
        ASSERT_TRUE(buf.commit(keep));
        EXPECT_EQ(buf.size(), keep);
        EXPECT_EQ(std::filesystem::file_size(path), keep);
    }

    // The committed bytes are still on disk after the buffer is gone.
    std::ifstream in(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, std::string(keep, 'A'));

    std::filesystem::remove(path);
}

TEST(MappedBackingTest, CommitTwiceFailsAfterFdClosed) {
    const auto path = tempFile("mapped_commit2");
    std::filesystem::remove(path);
    {
        MappedBuf buf;
        ASSERT_TRUE(buf.allocate(path, 1 * KB));
        ASSERT_TRUE(buf.commit(1));
        EXPECT_FALSE(buf.commit(1)); // backing fd already closed
        buf.release();
    }
    std::filesystem::remove(path);
}

TEST(MappedBackingTest, AllocateMissingDirectoryFails) {
    const auto path =
        tempFile("no_such_dir").parent_path() / "capio_filebuf_missing_dir" / "file.dat";
    MappedBuf buf;
    EXPECT_FALSE(buf.allocate(path, 1 * KB));
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.data(), nullptr);
}
TEST(MappedBackingTest, GrowKeepsDataInBackingFile) {
    const auto path = tempFile("mapped_grow");
    std::filesystem::remove(path);
    {
        MappedBuf buf;
        ASSERT_TRUE(buf.allocate(path, 1 * KB));
        std::memset(buf.data(), 'A', 1 * KB);

        ASSERT_NE(buf.grow(4 * KB, {{0, 1 * KB}}), nullptr);
        EXPECT_GE(buf.size(), 4 * KB);
        EXPECT_EQ(std::filesystem::file_size(path), static_cast<uintmax_t>(buf.size()));

        // Writes after the grow must still land in the file, so commit persists them.
        std::memset(buf.data() + 1 * KB, 'B', 1 * KB);
        ASSERT_TRUE(buf.commit(2 * KB));
        buf.release(); // must unmap the grown mapping cleanly
        EXPECT_TRUE(buf.empty());
    }

    std::ifstream in(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, std::string(1 * KB, 'A') + std::string(1 * KB, 'B'));

    std::filesystem::remove(path);
}

TEST(MappedBackingTest, GrowAfterCommitFails) {
    const auto path = tempFile("mapped_grow_committed");
    std::filesystem::remove(path);
    {
        MappedBuf buf;
        ASSERT_TRUE(buf.allocate(path, 1 * KB));
        ASSERT_TRUE(buf.commit(1 * KB));
        EXPECT_EQ(buf.grow(4 * KB, {{0, 1 * KB}}), nullptr);
        EXPECT_EQ(buf.size(), 1 * KB); // the old buffer is kept
    }
    std::filesystem::remove(path);
}
