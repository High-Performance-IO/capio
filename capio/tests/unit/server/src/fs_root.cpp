#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "utils/fs_root.hpp"

TEST(FsRootTest, UnknownRootMapsPathsToThemselves) {
    const FsRoot root;
    EXPECT_FALSE(root.known());
    EXPECT_TRUE(root.get().empty());
    EXPECT_EQ(root.real("/d/f"), "/d/f");
    EXPECT_EQ(root.virtualPath("/d/f"), "/d/f");
}

TEST(FsRootTest, VirtualPathsAreUnderTheRoot) {
    FsRoot root;
    EXPECT_TRUE(root.set("/mnt/shared/run"));
    EXPECT_TRUE(root.known());
    EXPECT_EQ(root.get(), "/mnt/shared/run");

    EXPECT_EQ(root.real("/"), "/mnt/shared/run/");
    EXPECT_EQ(root.real("/f"), "/mnt/shared/run/f");
    EXPECT_EQ(root.real("/d/f"), "/mnt/shared/run/d/f");
    EXPECT_EQ(root.real("/a b/f"), "/mnt/shared/run/a b/f");
}

TEST(FsRootTest, VirtualPathIsTheInverseOfReal) {
    FsRoot root;
    root.set("/mnt/shared/run");
    EXPECT_EQ(root.virtualPath("/mnt/shared/run"), "/");
    EXPECT_EQ(root.virtualPath("/mnt/shared/run/f"), "/f");
    EXPECT_EQ(root.virtualPath("/mnt/shared/run/d/f"), "/d/f");
    for (const std::filesystem::path path : {"/f", "/d/f", "/d/e/f", "/a b"}) {
        EXPECT_EQ(root.virtualPath(root.real(path)), path);
    }
}

TEST(FsRootTest, OnlyTheFirstRootCounts) {
    // -r wins over the first FS client, and the first FS client over the next ones
    FsRoot root;
    EXPECT_TRUE(root.set("/first"));
    EXPECT_FALSE(root.set("/second"));
    EXPECT_EQ(root.get(), "/first");
    EXPECT_EQ(root.real("/f"), "/first/f");
}

TEST(FsRootTest, ReadersSeeTheRootOnceItIsSet) {
    FsRoot root;
    std::atomic<bool> stop = false;
    std::atomic<int> torn  = 0;
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&] {
            while (!stop) {
                // Either not known yet, and the path maps to itself, or the whole root
                if (const auto real = root.real("/f"); real != "/f" && real != "/root/f") {
                    ++torn;
                }
            }
        });
    }
    root.set("/root");
    stop = true;
    for (auto &reader : readers) {
        reader.join();
    }
    EXPECT_EQ(torn, 0);
    EXPECT_EQ(root.real("/f"), "/root/f");
}

TEST(FsRootTest, TheServerRootIsUnsetInTheUnitTests) {
    // The other tests rely on it: paths are then used as they are
    EXPECT_FALSE(fs_root().known());
    EXPECT_EQ(fs_root().real("/tmp/f"), "/tmp/f");
}
