#include <gtest/gtest.h>

#include <cerrno>
#include <climits>
#include <filesystem>
#include <memory>

#include <fcntl.h>
#include <unistd.h>

TEST(SystemCallTest, TestFileCreateReopenClose) {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_WRONLY | O_TRUNC;
    int fd                         = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    fd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestCreat) {
    constexpr const char *PATHNAME = "test_file.txt";
    int fd                         = creat(PATHNAME, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestFileCreateReopenCloseWithOpenatAtFdcwd) {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_WRONLY | O_TRUNC;
    int fd                         = openat(AT_FDCWD, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    fd = openat(AT_FDCWD, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME, 0), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestOpenFailsWithOExclIfFileAlreadyExists) {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_WRONLY | O_TRUNC | O_EXCL;
    int fd                         = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    fd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_EQ(fd, -1);
    EXPECT_EQ(errno, EEXIST);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestFileCreateReopenCloseInDifferentDirectoryWithOpenatAbsolutePath) {
    const auto path_fs =
        std::filesystem::path(std::getenv("PWD")) / std::filesystem::path("test_file.txt");
    const char *PATHNAME = path_fs.c_str();
    int flags            = O_CREAT | O_WRONLY | O_TRUNC;
    int fd               = openat(0, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_EQ(faccessat(0, PATHNAME, F_OK, 0), 0);
    fd = openat(0, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlinkat(0, PATHNAME, 0), -1);
    EXPECT_NE(faccessat(0, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestFileCreateReopenCloseInDifferentDirectoryWithOpenatDirfd) {
    constexpr const char *PATHNAME = "test_file.txt";
    const char *DIRPATH            = std::getenv("PWD");
    int flags                      = O_RDONLY | O_DIRECTORY;
    int dirfd                      = open(DIRPATH, flags);
    EXPECT_NE(dirfd, -1);
    flags  = O_CREAT | O_WRONLY | O_TRUNC;
    int fd = openat(dirfd, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_EQ(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
    fd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlinkat(dirfd, PATHNAME, 0), -1);
    EXPECT_NE(close(dirfd), -1);
    EXPECT_NE(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
}

// An offset past the largest one fails, as one before the start of the file: Linux gives EINVAL
// for both, not EOVERFLOW (#322)
TEST(SystemCallTest, TestLseekPastTheLargestOffsetFails) {
    const int fd = open("lseek_overflow", O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(write(fd, "0123456789", 10), 10);
    EXPECT_EQ(lseek(fd, LLONG_MAX, SEEK_CUR), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(lseek(fd, LLONG_MAX, SEEK_END), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(lseek(fd, -11, SEEK_END), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 10); // a failed seek leaves the offset where it was
    EXPECT_EQ(lseek(fd, LLONG_MAX - 10, SEEK_END), LLONG_MAX);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("lseek_overflow"), 0);
}
