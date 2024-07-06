#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <sys/xattr.h>

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

TEST(SystemCallTest, TestFgetxattr) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    uid_t tmp;
    // WARNING: CAPIO does not support yet this system call
    //  but it needs to be intercepted nevertheless. It will always return -1 as of now.
    EXPECT_EQ(fgetxattr(fd, "system.posix_acl_access", &tmp, sizeof(uid_t)), -1);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestStatfs) {
    struct statfs buf;
    EXPECT_NE(statfs(".", &buf), -1);
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_NE(fstatfs(fd, &buf), -1);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}