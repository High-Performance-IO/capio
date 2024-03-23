#include <gtest/gtest.h>

#include <cerrno>
#include <climits>
#include <filesystem>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

TEST(SystemCallTest, TestDirectoryCreateReopenClose) {
    constexpr const char *PATHNAME = "test";
    EXPECT_NE(mkdir(PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    int flags = O_RDONLY | O_DIRECTORY;
    int fd    = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    fd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(rmdir(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDirectoryCreateReopenCloseWithMkdiratAtFdcwd) {
    constexpr const char *PATHNAME = "test";
    EXPECT_NE(mkdirat(AT_FDCWD, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    int flags = O_RDONLY | O_DIRECTORY;
    int fd    = openat(AT_FDCWD, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    fd = openat(AT_FDCWD, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestMkdirFailsIfDirectoryAlreadyExists) {
    constexpr const char *PATHNAME = "test";
    EXPECT_NE(mkdir(PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    EXPECT_EQ(mkdir(PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(errno, EEXIST);
    EXPECT_NE(rmdir(PATHNAME), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestDirectoryCreateReopenCloseInDifferentDirectoryWithOpenatAbsolutePath) {
    const auto path_fs = std::filesystem::path(std::getenv("PWD")) / std::filesystem::path("test");
    const char *PATHNAME = path_fs.c_str();
    EXPECT_NE(mkdirat(0, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(0, PATHNAME, F_OK, 0), 0);
    int flags = O_RDONLY | O_DIRECTORY;
    int fd    = openat(0, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    fd = openat(0, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlinkat(0, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(0, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestDirectoryCreateReopenCloseInDifferentDirectoryWithMkdiratDirfd) {
    constexpr const char *PATHNAME = "test";
    const char *DIRPATH            = std::getenv("PWD");
    int flags                      = O_RDONLY | O_DIRECTORY;
    int dirfd                      = open(DIRPATH, flags);
    EXPECT_NE(dirfd, -1);
    EXPECT_NE(mkdirat(dirfd, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
    int fd = openat(dirfd, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    fd = openat(dirfd, PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlinkat(dirfd, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
    EXPECT_NE(close(dirfd), -1);
}

TEST(SystemCallTest, TestGetcwd) {
    auto expected_path = std::string(std::getenv("PWD"));
    char obtained_path[PATH_MAX];
    EXPECT_NE(getcwd(obtained_path, PATH_MAX), nullptr);
    EXPECT_EQ(expected_path, std::string(obtained_path));
}

TEST(SystemCallTest, TestGetcwdWithPathLongerThanSize) {
    auto expected_path = std::string(std::getenv("PWD"));
    EXPECT_GT(expected_path.size(), 1);
    char obtained_path[1];
    EXPECT_EQ(getcwd(obtained_path, 1), nullptr);
    EXPECT_EQ(errno, ERANGE);
}
