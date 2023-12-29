#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void check_statbuf(struct stat &buf, unsigned long st_size) {
    REQUIRE(buf.st_blocks == 8);
    REQUIRE(buf.st_gid == getgid());
    REQUIRE(buf.st_size == static_cast<off_t>(st_size));
    REQUIRE(buf.st_uid == getuid());
}

TEST_CASE("Test stat syscall on file", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = open(PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(write(fd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(fd) != -1);
    struct stat statbuf {};
    REQUIRE(stat(PATHNAME, &statbuf) == 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test stat syscall on folder", "[syscall]") {
    constexpr const char *PATHNAME = "test";
    REQUIRE(mkdir(PATHNAME, S_IRWXU) != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    struct stat statbuf {};
    REQUIRE(stat(PATHNAME, &statbuf) == 0);
    check_statbuf(statbuf, 4096);
    REQUIRE(rmdir(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test stat syscall on nonexistent file", "[syscall]") {
    struct stat statbuf {};
    REQUIRE(stat("test", &statbuf) == -1);
    REQUIRE(errno == ENOENT);
}

TEST_CASE("Test fstat syscall on file", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = open(PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(write(fd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    struct stat statbuf {};
    REQUIRE(fstat(fd, &statbuf) == 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    REQUIRE(close(fd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test fstat syscall on folder", "[syscall]") {
    constexpr const char *PATHNAME = "test";
    REQUIRE(mkdir(PATHNAME, S_IRWXU) != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    int flags = O_RDONLY | O_DIRECTORY;
    int fd    = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    struct stat statbuf {};
    REQUIRE(fstat(fd, &statbuf) == 0);
    check_statbuf(statbuf, 4096);
    REQUIRE(close(fd) != -1);
    REQUIRE(rmdir(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test fstat syscall on invalid fd", "[syscall]") {
    struct stat statbuf {};
    REQUIRE(fstat(-1, &statbuf) == -1);
    REQUIRE(errno == EBADF);
}

TEST_CASE("Test fstatat syscall on file with AT_FDCWD", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = openat(AT_FDCWD, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) == 0);
    REQUIRE(write(fd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(fd) != -1);
    struct stat statbuf {};
    REQUIRE(fstatat(AT_FDCWD, PATHNAME, &statbuf, 0) == 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    REQUIRE(unlinkat(AT_FDCWD, PATHNAME, 0) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test fstatat syscall on folder with AT_FDCWD", "[syscall]") {
    constexpr const char *PATHNAME = "test";
    REQUIRE(mkdirat(AT_FDCWD, PATHNAME, S_IRWXU) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) == 0);
    struct stat statbuf {};
    REQUIRE(fstatat(AT_FDCWD, PATHNAME, &statbuf, 0) == 0);
    check_statbuf(statbuf, 4096);
    REQUIRE(unlinkat(AT_FDCWD, PATHNAME, AT_REMOVEDIR) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test fstatat syscall on file in a different directory using absolute path",
          "[syscall]") {
    const auto path_fs =
        std::filesystem::path(std::getenv("PWD")) / std::filesystem::path("test_file.txt");
    const char *PATHNAME = path_fs.c_str();
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = openat(0, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(faccessat(0, PATHNAME, F_OK, 0) == 0);
    REQUIRE(write(fd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(fd) != -1);
    struct stat statbuf {};
    REQUIRE(fstatat(0, PATHNAME, &statbuf, 0) == 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    REQUIRE(unlinkat(0, PATHNAME, 0) != -1);
    REQUIRE(faccessat(0, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test fstatat syscall on folder in a different directory using absolute path",
          "[syscall]") {
    const auto path_fs = std::filesystem::path(std::getenv("PWD")) / std::filesystem::path("test");
    const char *PATHNAME = path_fs.c_str();
    REQUIRE(mkdirat(0, PATHNAME, S_IRWXU) != -1);
    REQUIRE(faccessat(0, PATHNAME, F_OK, 0) == 0);
    struct stat statbuf {};
    REQUIRE(fstatat(0, PATHNAME, &statbuf, 0) == 0);
    check_statbuf(statbuf, 4096);
    REQUIRE(unlinkat(0, PATHNAME, AT_REMOVEDIR) != -1);
    REQUIRE(faccessat(0, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test fstatat syscall on file in a different directory using dirfd", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    const char *DIRPATH            = std::getenv("PWD");
    int dirfd                      = open(DIRPATH, O_RDONLY | O_DIRECTORY);
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = openat(dirfd, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(faccessat(dirfd, PATHNAME, F_OK, 0) == 0);
    REQUIRE(write(fd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(fd) != -1);
    struct stat statbuf {};
    REQUIRE(fstatat(dirfd, PATHNAME, &statbuf, 0) == 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    REQUIRE(unlinkat(dirfd, PATHNAME, 0) != -1);
    REQUIRE(faccessat(dirfd, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test fstatat syscall on folder in a different directory using dirfd", "[syscall]") {
    constexpr const char *PATHNAME = "test";
    const char *DIRPATH            = std::getenv("PWD");
    int flags                      = O_RDONLY | O_DIRECTORY;
    int dirfd                      = open(DIRPATH, flags);
    REQUIRE(dirfd != -1);
    REQUIRE(mkdirat(dirfd, PATHNAME, S_IRWXU) != -1);
    REQUIRE(faccessat(dirfd, PATHNAME, F_OK, 0) == 0);
    struct stat statbuf {};
    REQUIRE(fstatat(dirfd, PATHNAME, &statbuf, 0) == 0);
    check_statbuf(statbuf, 4096);
    REQUIRE(unlinkat(dirfd, PATHNAME, AT_REMOVEDIR) != -1);
    REQUIRE(faccessat(dirfd, PATHNAME, F_OK, 0) != 0);
    REQUIRE(close(dirfd) != -1);
}

TEST_CASE("Test fstatat syscall using AT_EMPTY_PATH and AT_FDCWD", "[syscall]") {
    struct stat statbuf {};
    REQUIRE(fstatat(AT_FDCWD, "", &statbuf, AT_EMPTY_PATH) == 0);
    check_statbuf(statbuf, 4096);
}

TEST_CASE("Test fstatat syscall on file using AT_EMPTY_PATH and dirfd", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int dirfd = openat(AT_FDCWD, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) == 0);
    REQUIRE(write(dirfd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    struct stat statbuf {};
    REQUIRE(fstatat(dirfd, "", &statbuf, AT_EMPTY_PATH) == 0);
    check_statbuf(statbuf, strlen(BUFFER) * sizeof(char));
    REQUIRE(close(dirfd) != -1);
    REQUIRE(unlinkat(AT_FDCWD, PATHNAME, 0) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test fstatat syscall on folder using AT_EMPTY_PATH and dirfd", "[syscall]") {
    constexpr const char *PATHNAME = "test";
    REQUIRE(mkdirat(AT_FDCWD, PATHNAME, S_IRWXU) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) == 0);
    int dirfd = openat(AT_FDCWD, PATHNAME, O_RDONLY | O_DIRECTORY);
    REQUIRE(dirfd != -1);
    struct stat statbuf {};
    REQUIRE(fstatat(dirfd, "", &statbuf, AT_EMPTY_PATH) == 0);
    check_statbuf(statbuf, 4096);
    REQUIRE(close(dirfd) != -1);
    REQUIRE(unlinkat(AT_FDCWD, PATHNAME, AT_REMOVEDIR) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test fstatat syscall on nonexistent file", "[syscall]") {
    struct stat statbuf {};
    REQUIRE(fstatat(AT_FDCWD, "test", &statbuf, 0) == -1);
    REQUIRE(errno == ENOENT);
}

TEST_CASE("Test fstatat syscall on relative path with invalid dirfd", "[syscall]") {
    constexpr const char *PATHNAME = "test";
    struct stat statbuf {};
    REQUIRE(fstatat(-1, PATHNAME, &statbuf, 0) == -1);
    REQUIRE(errno == EBADF);
}

TEST_CASE("Test fstatat syscall with empty path and no AT_EMPTY_PATH flag", "[syscall]") {
    struct stat statbuf {};
    REQUIRE(fstatat(AT_FDCWD, "", &statbuf, 0) == -1);
    REQUIRE(errno == ENOENT);
}

TEST_CASE("Test file creation, write and close using stat", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr int ARRAY_SIZE       = 100;
    int array[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }
    FILE *fp = fopen(PATHNAME, "w+");
    REQUIRE(fp != nullptr);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(fwrite(array, sizeof(int), ARRAY_SIZE, fp) == ARRAY_SIZE);
    REQUIRE(fseek(fp, 0, SEEK_SET) != -1);
    int num;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        REQUIRE(fread(&num, sizeof(int), 1, fp) == 1);
        REQUIRE(num == i);
    }
    REQUIRE(fread(&num, sizeof(int), 1, fp) == 0);
    REQUIRE(feof(fp) != 0);
    REQUIRE(fclose(fp) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test directory creation, reopening and close with stat", "[syscall]") {
    constexpr const char *PATHNAME = "test";
    REQUIRE(mkdir(PATHNAME, S_IRWXU) != -1);
    FILE *fp = fopen(PATHNAME, "r");
    REQUIRE(fp != nullptr);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(fclose(fp) != -1);
    fp = fopen(PATHNAME, "r");
    REQUIRE(fp != nullptr);
    REQUIRE(fclose(fp) != -1);
    REQUIRE(rmdir(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}