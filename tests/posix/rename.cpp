#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

TEST_CASE("Test file rename when the new path does not exist", "[posix]") {
    constexpr const char* OLDNAME = "test_file.txt";
    constexpr const char* NEWNAME = "test_file2.txt";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int fd = open(OLDNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(close(fd) != -1);
    REQUIRE(access(OLDNAME, F_OK) == 0);
    REQUIRE(access(NEWNAME, F_OK) != 0);
    REQUIRE(rename(OLDNAME, NEWNAME) == 0);
    REQUIRE(access(OLDNAME, F_OK) != 0);
    REQUIRE(access(NEWNAME, F_OK) == 0);
    REQUIRE(unlink(NEWNAME) != -1);
    REQUIRE(access(NEWNAME, F_OK) != 0);
}

TEST_CASE("Test file rename when the new path already exists", "[posix]") {
    constexpr const char* OLDNAME = "test_file.txt";
    constexpr const char* NEWNAME = "test_file2.txt";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int fd = open(OLDNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(close(fd) != -1);
    fd = open(NEWNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(close(fd) != -1);
    REQUIRE(access(OLDNAME, F_OK) == 0);
    REQUIRE(access(NEWNAME, F_OK) == 0);
    REQUIRE(rename(OLDNAME, NEWNAME) == 0);
    REQUIRE(access(OLDNAME, F_OK) != 0);
    REQUIRE(access(NEWNAME, F_OK) == 0);
    REQUIRE(unlink(NEWNAME) != -1);
    REQUIRE(access(NEWNAME, F_OK) != 0);
}

TEST_CASE("Test directory rename when the new path does not exist", "[posix]") {
    constexpr const char* OLDNAME = "test";
    constexpr const char* NEWNAME = "test2";
    REQUIRE(mkdir(OLDNAME, S_IRWXU) != -1);
    REQUIRE(access(OLDNAME, F_OK) == 0);
    REQUIRE(access(NEWNAME, F_OK) != 0);
    REQUIRE(rename(OLDNAME, NEWNAME) == 0);
    REQUIRE(access(OLDNAME, F_OK) != 0);
    REQUIRE(access(NEWNAME, F_OK) == 0);
    REQUIRE(rmdir(NEWNAME) != -1);
    REQUIRE(access(NEWNAME, F_OK) != 0);
}

TEST_CASE("Test directory rename when the new path already exists", "[posix]") {
    constexpr const char* OLDNAME = "test";
    constexpr const char* NEWNAME = "test2";
    REQUIRE(mkdir(OLDNAME, S_IRWXU) != -1);
    REQUIRE(mkdir(NEWNAME, S_IRWXU) != -1);
    REQUIRE(access(OLDNAME, F_OK) == 0);
    REQUIRE(access(NEWNAME, F_OK) == 0);
    REQUIRE(rename(OLDNAME, NEWNAME) == 0);
    REQUIRE(access(OLDNAME, F_OK) != 0);
    REQUIRE(access(NEWNAME, F_OK) == 0);
    REQUIRE(rmdir(NEWNAME) != -1);
    REQUIRE(access(NEWNAME, F_OK) != 0);
}
