#include "storage/allocator.hpp"

#include <sys/stat.h>

#include <cstring>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {
constexpr off64_t KB = 1024;
constexpr off64_t MB = 1024 * KB;

std::filesystem::path tempFile(const char *suffix) {
    return std::filesystem::temp_directory_path() / ("capio_alloc_" + std::string(suffix) + ".dat");
}
} // namespace

using HeapAlloc   = MemoryAllocator<HeapAllocator>;
using MappedAlloc = MemoryAllocator<MappedAllocator>;

// ---- HeapAllocator -------------------------------------------------------

TEST(HeapAllocatorTest, StartsEmpty) {
    HeapAlloc alloc;
    EXPECT_TRUE(alloc.empty());
    EXPECT_EQ(alloc.data(), nullptr);
    EXPECT_EQ(alloc.size(), 0);
}

TEST(HeapAllocatorTest, AllocateAndRelease) {
    HeapAlloc alloc;
    EXPECT_TRUE(alloc.allocate(1 * MB));
    EXPECT_FALSE(alloc.empty());
    ASSERT_NE(alloc.data(), nullptr);
    EXPECT_EQ(alloc.size(), 1 * MB);

    // The buffer is real, writable heap memory.
    std::memset(alloc.data(), 0xAB, alloc.size());

    alloc.release();
    EXPECT_TRUE(alloc.empty());
    EXPECT_EQ(alloc.data(), nullptr);
    EXPECT_EQ(alloc.size(), 0);
}

TEST(HeapAllocatorTest, AllocateTwiceReturnsFalse) {
    HeapAlloc alloc;
    ASSERT_TRUE(alloc.allocate(1 * KB));
    EXPECT_FALSE(alloc.allocate(1 * MB));
    EXPECT_EQ(alloc.size(), 1 * KB);
}

TEST(HeapAllocatorTest, ResizeGrowsAndKeepsData) {
    HeapAlloc alloc;
    ASSERT_TRUE(alloc.allocate(1 * KB));
    for (off64_t i = 0; i < alloc.size(); ++i) {
        alloc.data()[i] = static_cast<char>(i & 0xFF);
    }

    char *const nb = alloc.resize(4 * KB, {{0, 1 * KB}});
    EXPECT_EQ(nb, alloc.data());
    EXPECT_GE(alloc.size(), 4 * KB);

    // Data written before the resize must survive at the same offsets.
    for (off64_t i = 0; i < 1 * KB; ++i) {
        EXPECT_EQ(alloc.data()[i], static_cast<char>(i & 0xFF));
    }
}

TEST(HeapAllocatorTest, ReleaseIsSafeTwice) {
    HeapAlloc alloc;
    ASSERT_TRUE(alloc.allocate(1 * KB));
    alloc.release();
    alloc.release(); // double-free must not crash
    alloc.release();
}

// ---- MappedAllocator -----------------------------------------------------

TEST(MappedAllocatorTest, AllocateCreatesBackingFile) {
    const auto path = tempFile("mapped_creates");
    std::filesystem::remove(path);
    {
        MappedAlloc alloc;
        ASSERT_TRUE(alloc.allocate(path, 1 * MB));
        EXPECT_FALSE(alloc.empty());
        ASSERT_NE(alloc.data(), nullptr);
        EXPECT_EQ(alloc.size(), 1 * MB);
        EXPECT_EQ(std::filesystem::file_size(path), 1 * MB);

        std::memset(alloc.data(), 0x5A, alloc.size()); // MAP_SHARED: writable
    }
    std::filesystem::remove(path);
}

TEST(MappedAllocatorTest, CommitTruncatesAndPersists) {
    const auto path        = tempFile("mapped_commit");
    constexpr off64_t keep = 16;
    std::filesystem::remove(path);
    {
        MappedAlloc alloc;
        ASSERT_TRUE(alloc.allocate(path, 1 * MB));

        std::memset(alloc.data(), 'A', keep);
        ASSERT_TRUE(alloc.commit(keep));
        EXPECT_EQ(alloc.size(), keep);
        EXPECT_EQ(std::filesystem::file_size(path), keep);
    }

    // The committed bytes are still on disk after the buffer is gone.
    std::ifstream in(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, std::string(keep, 'A'));

    std::filesystem::remove(path);
}

TEST(MappedAllocatorTest, CommitTwiceFailsAfterFdClosed) {
    const auto path = tempFile("mapped_commit2");
    std::filesystem::remove(path);
    {
        MappedAlloc alloc;
        ASSERT_TRUE(alloc.allocate(path, 1 * KB));
        ASSERT_TRUE(alloc.commit(1));
        EXPECT_FALSE(alloc.commit(1)); // backing fd already closed
        alloc.release();
    }
    std::filesystem::remove(path);
}

TEST(MappedAllocatorTest, AllocateMissingDirectoryFails) {
    const auto path =
        tempFile("no_such_dir").parent_path() / "capio_alloc_missing_dir" / "file.dat";
    MappedAlloc alloc;
    EXPECT_FALSE(alloc.allocate(path, 1 * KB));
    EXPECT_TRUE(alloc.empty());
    EXPECT_EQ(alloc.data(), nullptr);
}