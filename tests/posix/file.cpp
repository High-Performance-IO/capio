#include <cerrno>
#include <filesystem>
#include <memory>

#include <fcntl.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Test file creation, reopening, and close", "[posix]") {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int fd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    fd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test file creation using creat system call", "[posix]") {
    constexpr const char *PATHNAME = "test_file.txt";
    int fd = creat(PATHNAME, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test file creation, reopening, and close using openat with AT_FDCWD", "[posix]") {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int fd = openat(AT_FDCWD, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) == 0);
    fd = openat(AT_FDCWD, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(unlinkat(AT_FDCWD, PATHNAME, 0) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test that open O_EXCL fails if file already exists", "[posix]") {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags = O_CREAT | O_WRONLY | O_TRUNC | O_EXCL;
    int fd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    fd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd == -1);
    REQUIRE(errno == EEXIST);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE(
    "Test file creation, reopen and close in a different directory using openat with absolute path",
    "[posix]") {
    const auto path_fs =
        std::filesystem::path(std::getenv("PWD")) / std::filesystem::path("test_file.txt");
    const char *PATHNAME = path_fs.c_str();
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int fd = openat(0, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(faccessat(0, PATHNAME, F_OK, 0) == 0);
    fd = openat(0, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(unlinkat(0, PATHNAME, 0) != -1);
    REQUIRE(faccessat(0, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test file creation, reopen and close in a different directory using openat with dirfd",
          "[posix]") {
    constexpr const char *PATHNAME = "test_file.txt";
    const char *DIRPATH = std::getenv("PWD");
    int flags = O_RDONLY | O_DIRECTORY;
    int dirfd = open(DIRPATH, flags);
    REQUIRE(dirfd != -1);
    flags = O_CREAT | O_WRONLY | O_TRUNC;
    int fd = openat(dirfd, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(faccessat(dirfd, PATHNAME, F_OK, 0) == 0);
    fd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(unlinkat(dirfd, PATHNAME, 0) != -1);
    REQUIRE(close(dirfd) != -1);
    REQUIRE(faccessat(dirfd, PATHNAME, F_OK, 0) != 0);
}