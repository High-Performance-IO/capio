#include <gtest/gtest.h>

#include <filesystem>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void check_statxbuf(struct statx &buf, unsigned long st_size) {
    EXPECT_EQ(buf.stx_blocks, 8);
    EXPECT_EQ(buf.stx_gid, getgid());
    EXPECT_EQ(buf.stx_size, st_size);
    EXPECT_EQ(buf.stx_uid, getuid());
}

TEST(SystemCallTest, TestStatxOnFileWithAtFdcwd) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = openat(AT_FDCWD, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    EXPECT_EQ(write(fd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    EXPECT_NE(close(fd), -1);
    struct statx statxbuf {};
    EXPECT_EQ(statx(AT_FDCWD, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), 0);
    check_statxbuf(statxbuf, strlen(BUFFER) * sizeof(char));
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME, 0), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestStatxOnDirectoryWithAtFdcwd) {
    constexpr const char *PATHNAME = "test";
    EXPECT_NE(mkdirat(AT_FDCWD, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    struct statx statxbuf {};
    EXPECT_EQ(statx(AT_FDCWD, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), 0);
    check_statxbuf(statxbuf, 4096);
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestStatxOnFileInDifferentDirectoryWithAbsolutePath) {
    const auto path_fs =
        std::filesystem::path(std::getenv("PWD")) / std::filesystem::path("test_file.txt");
    const char *PATHNAME = path_fs.c_str();
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = openat(0, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(faccessat(0, PATHNAME, F_OK, 0), 0);
    EXPECT_EQ(write(fd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    EXPECT_NE(close(fd), -1);
    struct statx statxbuf {};
    EXPECT_EQ(statx(AT_FDCWD, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), 0);
    check_statxbuf(statxbuf, strlen(BUFFER) * sizeof(char));
    EXPECT_NE(unlinkat(0, PATHNAME, 0), -1);
    EXPECT_NE(faccessat(0, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestStatxOnDirectoryInDifferentDirectoryWithAbsoluePath) {
    const auto path_fs = std::filesystem::path(std::getenv("PWD")) / std::filesystem::path("test");
    const char *PATHNAME = path_fs.c_str();
    EXPECT_NE(mkdirat(0, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(0, PATHNAME, F_OK, 0), 0);
    struct statx statxbuf {};
    EXPECT_EQ(statx(AT_FDCWD, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), 0);
    check_statxbuf(statxbuf, 4096);
    EXPECT_NE(unlinkat(0, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(0, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestStatxOnFileInDifferentDirectoryWithDirfd) {
    constexpr const char *PATHNAME = "test_file.txt";
    const char *DIRPATH            = std::getenv("PWD");
    int dirfd                      = open(DIRPATH, O_RDONLY | O_DIRECTORY);
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = openat(dirfd, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
    EXPECT_EQ(write(fd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    EXPECT_NE(close(fd), -1);
    struct statx statxbuf {};
    EXPECT_EQ(statx(dirfd, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), 0);
    check_statxbuf(statxbuf, strlen(BUFFER) * sizeof(char));
    EXPECT_NE(unlinkat(dirfd, PATHNAME, 0), -1);
    EXPECT_NE(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestStatxOnDirectoryInDifferentDirectoryWithDirfd) {
    constexpr const char *PATHNAME = "test";
    const char *DIRPATH            = std::getenv("PWD");
    int flags                      = O_RDONLY | O_DIRECTORY;
    int dirfd                      = open(DIRPATH, flags);
    EXPECT_NE(dirfd, -1);
    EXPECT_NE(mkdirat(dirfd, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
    struct statx statxbuf {};
    EXPECT_EQ(statx(dirfd, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), 0);
    check_statxbuf(statxbuf, 4096);
    EXPECT_NE(unlinkat(dirfd, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
    EXPECT_NE(close(dirfd), -1);
}

TEST(SystemCallTest, TestStatxWithAtEmptyPathAndAtFdcwd) {
    struct statx statxbuf {};
    EXPECT_EQ(statx(AT_FDCWD, "", AT_EMPTY_PATH, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), 0);
    check_statxbuf(statxbuf, 4096);
}

TEST(SystemCallTest, TestStatxOnFileWithAtEmptyPathAndDirfd) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int dirfd = openat(AT_FDCWD, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    EXPECT_EQ(write(dirfd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    struct statx statxbuf {};
    EXPECT_EQ(statx(dirfd, "", AT_EMPTY_PATH, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), 0);
    check_statxbuf(statxbuf, strlen(BUFFER) * sizeof(char));
    EXPECT_NE(close(dirfd), -1);
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME, 0), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestStatxOnDirectoryWithAtEmptyPathAndDirfd) {
    constexpr const char *PATHNAME = "test";
    EXPECT_NE(mkdirat(AT_FDCWD, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    int dirfd = openat(AT_FDCWD, PATHNAME, O_RDONLY | O_DIRECTORY);
    EXPECT_NE(dirfd, -1);
    struct statx statxbuf {};
    EXPECT_EQ(statx(dirfd, "", AT_EMPTY_PATH, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), 0);
    check_statxbuf(statxbuf, 4096);
    EXPECT_NE(close(dirfd), -1);
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestStatxOnNonexistentFile) {
    struct statx statxbuf {};
    EXPECT_EQ(statx(AT_FDCWD, "test", 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST(SystemCallTest, TestStatxOnRelativePathWithInvalidDirfd) {
    constexpr const char *PATHNAME = "test";
    struct statx statxbuf {};
    EXPECT_EQ(statx(-1, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(SystemCallTest, TestStatxWithStatxReservedSet) {
    constexpr const char *PATHNAME = "test";
    struct statx statxbuf {};
    EXPECT_EQ(statx(-1, PATHNAME, 0, STATX__RESERVED, &statxbuf), -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST(SystemCallTest, TestStatxWithEmptyPathAndNoEmptyPath) {
    struct statx statxbuf {};
    EXPECT_EQ(statx(AT_FDCWD, "", 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf), -1);
    EXPECT_EQ(errno, ENOENT);
}