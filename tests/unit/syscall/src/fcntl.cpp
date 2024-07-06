#include <gtest/gtest.h>

#include <fcntl.h>

TEST(SystemCallTest, TestFchmod) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(fchmod(fd, S_IRWXU), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFchown) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(fchown(fd, getuid(), getgid()), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestStatfs) {}