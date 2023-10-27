#include <cerrno>
#include <climits>
#include <filesystem>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Test directory creation, reopening and close", "[posix]") {
    constexpr const char *PATHNAME = "test";
    REQUIRE(mkdir(PATHNAME, S_IRWXU) != -1);
    int flags = O_RDONLY | O_DIRECTORY;
    int fd    = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    fd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(rmdir(PATHNAME) != -1);
}

TEST_CASE("Test directory creation, reopening, and close using mkdirat with AT_FDCWD", "[posix]") {
    constexpr const char *PATHNAME = "test";
    REQUIRE(mkdirat(AT_FDCWD, PATHNAME, S_IRWXU) != -1);
    int flags = O_RDONLY | O_DIRECTORY;
    int fd    = openat(AT_FDCWD, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    fd = openat(AT_FDCWD, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(unlinkat(AT_FDCWD, PATHNAME, AT_REMOVEDIR) != -1);
}

TEST_CASE("Test that mkdir fails if directory already exists", "[posix]") {
    constexpr const char *PATHNAME = "test";
    REQUIRE(mkdir(PATHNAME, S_IRWXU) != -1);
    int flags = O_RDONLY | O_DIRECTORY;
    int fd    = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(mkdir(PATHNAME, S_IRWXU) == -1);
    REQUIRE(errno == EEXIST);
    REQUIRE(rmdir(PATHNAME) != -1);
}

TEST_CASE("Test directory creation, reopening, and close in a different directory using openat "
          "with absolute path",
          "[posix]") {
    const auto path_fs = std::filesystem::path(std::getenv("PWD")) / std::filesystem::path("test");
    const char *PATHNAME = path_fs.c_str();
    REQUIRE(mkdirat(0, PATHNAME, S_IRWXU) != -1);
    int flags = O_RDONLY | O_DIRECTORY;
    int fd    = openat(0, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    fd = openat(0, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(unlinkat(0, PATHNAME, AT_REMOVEDIR) != -1);
}

TEST_CASE("Test directory creation, reopening, and close in a different directory using mkdirat "
          "with dirfd",
          "[posix]") {
    constexpr const char *PATHNAME = "test";
    const char *DIRPATH            = std::getenv("PWD");
    int flags                      = O_RDONLY | O_DIRECTORY;
    int dirfd                      = open(DIRPATH, flags);
    REQUIRE(dirfd != -1);
    REQUIRE(mkdirat(dirfd, PATHNAME, S_IRWXU) != -1);
    int fd = openat(dirfd, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    fd = openat(dirfd, PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(close(fd) != -1);
    REQUIRE(unlinkat(dirfd, PATHNAME, AT_REMOVEDIR) != -1);
    REQUIRE(close(dirfd) != -1);
}

/*
TEST_CASE("Test obtaining the current directory with getcwd system call", "[posix]") {
    auto expected_path = std::string(std::getenv("PWD"));
    char obtained_path[PATH_MAX];
    getcwd(obtained_path, PATH_MAX);
    REQUIRE(expected_path == std::string(obtained_path));
}
*/