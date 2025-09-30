#include <gtest/gtest.h>

#include <filesystem>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void check_statbuf(struct stat &buf, unsigned long st_size) {
    EXPECT_EQ(buf.st_blocks, 8);
    EXPECT_EQ(buf.st_gid, getgid());
    EXPECT_EQ(buf.st_size, static_cast<off_t>(st_size));
    EXPECT_EQ(buf.st_uid, getuid());
}

TEST(SystemCallTest, TestStatOnFile) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = open(PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(write(fd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    EXPECT_NE(close(fd), -1);
    struct stat statbuf {};
    EXPECT_EQ(stat(PATHNAME, &statbuf), 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestStatOnDirectory) {
    constexpr const char *PATHNAME = "test";
    EXPECT_NE(mkdir(PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    struct stat statbuf {};
    EXPECT_EQ(stat(PATHNAME, &statbuf), 0);
    check_statbuf(statbuf, 4096);
    EXPECT_NE(rmdir(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestStatOnNonexistentFile) {
    struct stat statbuf {};
    EXPECT_EQ(stat("test", &statbuf), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST(SystemCallTest, TestFstatOnFile) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = open(PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(write(fd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    struct stat statbuf {};
    EXPECT_EQ(fstat(fd, &statbuf), 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestFstatOnDirectory) {
    constexpr const char *PATHNAME = "test";
    EXPECT_NE(mkdir(PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    int flags = O_RDONLY | O_DIRECTORY;
    int fd    = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    struct stat statbuf {};
    EXPECT_EQ(fstat(fd, &statbuf), 0);
    check_statbuf(statbuf, 4096);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(rmdir(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestFstatOnInvalidFd) {
    struct stat statbuf {};
    EXPECT_EQ(fstat(-1, &statbuf), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(SystemCallTest, TestFstatatOnFileWithAtFdcwd) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = openat(AT_FDCWD, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    EXPECT_EQ(write(fd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    EXPECT_NE(close(fd), -1);
    struct stat statbuf {};
    EXPECT_EQ(fstatat(AT_FDCWD, PATHNAME, &statbuf, 0), 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME, 0), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestFstatatOnDirectoryWithAtFdcwd) {
    constexpr const char *PATHNAME = "test";
    EXPECT_NE(mkdirat(AT_FDCWD, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    struct stat statbuf {};
    EXPECT_EQ(fstatat(AT_FDCWD, PATHNAME, &statbuf, 0), 0);
    check_statbuf(statbuf, 4096);
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestFstatatOnFileInDifferentDirectoryWithAbsolutePath) {
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
    struct stat statbuf {};
    EXPECT_EQ(fstatat(0, PATHNAME, &statbuf, 0), 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    EXPECT_NE(unlinkat(0, PATHNAME, 0), -1);
    EXPECT_NE(faccessat(0, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestFstatatOnDirectoryInDifferentDirectoryWithAbsolutePath) {
    const auto path_fs = std::filesystem::path(std::getenv("PWD")) / std::filesystem::path("test");
    const char *PATHNAME = path_fs.c_str();
    EXPECT_NE(mkdirat(0, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(0, PATHNAME, F_OK, 0), 0);
    struct stat statbuf {};
    EXPECT_EQ(fstatat(0, PATHNAME, &statbuf, 0), 0);
    check_statbuf(statbuf, 4096);
    EXPECT_NE(unlinkat(0, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(0, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestFstatatOnFileInDifferentDirectoryWithDirfd) {
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
    struct stat statbuf {};
    EXPECT_EQ(fstatat(dirfd, PATHNAME, &statbuf, 0), 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    EXPECT_NE(unlinkat(dirfd, PATHNAME, 0), -1);
    EXPECT_NE(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestFstatatOnDirectoryInDifferentDirectoryWithDirfd) {
    constexpr const char *PATHNAME = "test";
    const char *DIRPATH            = std::getenv("PWD");
    int flags                      = O_RDONLY | O_DIRECTORY;
    int dirfd                      = open(DIRPATH, flags);
    EXPECT_NE(dirfd, -1);
    EXPECT_NE(mkdirat(dirfd, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
    struct stat statbuf {};
    EXPECT_EQ(fstatat(dirfd, PATHNAME, &statbuf, 0), 0);
    check_statbuf(statbuf, 4096);
    EXPECT_NE(unlinkat(dirfd, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(dirfd, PATHNAME, F_OK, 0), 0);
    EXPECT_NE(close(dirfd), -1);
}

TEST(SystemCallTest, TwstFstatatWithAtEmptyPathAndAtFdcwd) {
    struct stat statbuf {};
    EXPECT_EQ(fstatat(AT_FDCWD, "", &statbuf, AT_EMPTY_PATH), 0);
    check_statbuf(statbuf, 4096);
}

TEST(SystemCallTest, TestFstatatOnFileWithAtEmptyPathAndDirfd) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int dirfd = openat(AT_FDCWD, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    EXPECT_EQ(write(dirfd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    struct stat statbuf {};
    EXPECT_EQ(fstatat(dirfd, "", &statbuf, AT_EMPTY_PATH), 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    EXPECT_NE(close(dirfd), -1);
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME, 0), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestFstatatOnDirectoryWIthAtEmptyPathAndDirfd) {
    constexpr const char *PATHNAME = "test";
    EXPECT_NE(mkdirat(AT_FDCWD, PATHNAME, S_IRWXU), -1);
    EXPECT_EQ(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
    int dirfd = openat(AT_FDCWD, PATHNAME, O_RDONLY | O_DIRECTORY);
    EXPECT_NE(dirfd, -1);
    struct stat statbuf {};
    EXPECT_EQ(fstatat(dirfd, "", &statbuf, AT_EMPTY_PATH), 0);
    check_statbuf(statbuf, 4096);
    EXPECT_NE(close(dirfd), -1);
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME, AT_REMOVEDIR), -1);
    EXPECT_NE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0), 0);
}

TEST(SystemCallTest, TestFstatatOnNonexistentFile) {
    struct stat statbuf {};
    EXPECT_EQ(fstatat(AT_FDCWD, "test", &statbuf, 0), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST(SystemCallTest, TestFstatatOnRelativePathWithInvalidDirfd) {
    constexpr const char *PATHNAME = "test";
    struct stat statbuf {};
    EXPECT_EQ(fstatat(-1, PATHNAME, &statbuf, 0), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(SystemCallTest, TestFstatatWithEmptyPathAndNoAtEmptyPath) {
    struct stat statbuf {};
    EXPECT_EQ(fstatat(AT_FDCWD, "", &statbuf, 0), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST(SystemCallTest, TestFileCreateWriteCloseWithStat) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr int ARRAY_SIZE       = 100;
    int array[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }
    FILE *fp = fopen(PATHNAME, "w+");
    EXPECT_NE(fp, nullptr);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(fwrite(array, sizeof(int), ARRAY_SIZE, fp), ARRAY_SIZE);
    EXPECT_EQ(ftell(fp), ARRAY_SIZE * sizeof(int));
    EXPECT_NE(fseek(fp, 0, SEEK_SET), -1);
    int num;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        EXPECT_EQ(fread(&num, sizeof(int), 1, fp), 1);
        EXPECT_EQ(num, i);
    }
    EXPECT_EQ(fread(&num, sizeof(int), 1, fp), 0);
    EXPECT_NE(feof(fp), 0);
    EXPECT_NE(fclose(fp), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDirectoryCreateReopenCloseWithStat) {
    constexpr const char *PATHNAME = "test";
    EXPECT_NE(mkdir(PATHNAME, S_IRWXU), -1);
    FILE *fp = fopen(PATHNAME, "r");
    EXPECT_NE(fp, nullptr);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_NE(fclose(fp), -1);
    fp = fopen(PATHNAME, "r");
    EXPECT_NE(fp, nullptr);
    EXPECT_NE(fclose(fp), -1);
    EXPECT_NE(rmdir(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}