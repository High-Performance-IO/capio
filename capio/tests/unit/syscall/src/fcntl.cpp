#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <vector>
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
// F_GETFL returned the flags as a bool, so 1 (O_WRONLY) for any read-only descriptor with a flag
// set, and fdopendir refused a directory it took for write-only.
TEST(SystemCallTest, TestFcntlGetflReturnsTheAccessMode) {
    int fd = open("getfl.txt", O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(fcntl(fd, F_GETFL) & O_ACCMODE, O_RDWR);
    EXPECT_EQ(close(fd), 0);

    fd = open("getfl.txt", O_RDONLY | O_NONBLOCK);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(fcntl(fd, F_GETFL) & O_ACCMODE, O_RDONLY);
    EXPECT_NE(fcntl(fd, F_GETFL) & O_NONBLOCK, 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("getfl.txt"), 0);
}

// fdopendir checks the access mode with F_GETFL first, and refused a directory it took for
// write-only. tar lists a directory this way.
TEST(SystemCallTest, TestFdopendirOnOpenat) {
    ASSERT_EQ(mkdir("fdopendir_dir", S_IRWXU), 0);
    int fd = open("fdopendir_dir/a", O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(close(fd), 0);

    int dir_fd = openat(AT_FDCWD, "fdopendir_dir",
                        O_RDONLY | O_NOCTTY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC | O_DIRECTORY);
    ASSERT_NE(dir_fd, -1);
    DIR *dir = fdopendir(dir_fd);
    ASSERT_NE(dir, nullptr) << strerror(errno);
    std::vector<std::string> names;
    while (const dirent *entry = readdir(dir)) {
        names.emplace_back(entry->d_name);
    }
    EXPECT_EQ(closedir(dir), 0);
    EXPECT_NE(std::find(names.begin(), names.end(), "a"), names.end());

    EXPECT_EQ(unlink("fdopendir_dir/a"), 0);
    EXPECT_NE(rmdir("fdopendir_dir"), -1);
}
