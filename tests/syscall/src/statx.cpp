#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void check_statxbuf(struct statx &buf, unsigned long st_size) {
    REQUIRE(buf.stx_blocks == 8);
    REQUIRE(buf.stx_gid == getgid());
    REQUIRE(buf.stx_size == st_size);
    REQUIRE(buf.stx_uid == getuid());
}

TEST_CASE("Test statx syscall on file with AT_FDCWD", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = openat(AT_FDCWD, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) == 0);
    REQUIRE(write(fd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(fd) != -1);
    struct statx statxbuf {};
    REQUIRE(statx(AT_FDCWD, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf) == 0);
    check_statxbuf(statxbuf, strlen(BUFFER) * sizeof(char));
    REQUIRE(unlinkat(AT_FDCWD, PATHNAME, 0) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test statx syscall on folder with AT_FDCWD", "[syscall]") {
    constexpr const char *PATHNAME = "test";
    REQUIRE(mkdirat(AT_FDCWD, PATHNAME, S_IRWXU) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) == 0);
    struct statx statxbuf {};
    REQUIRE(statx(AT_FDCWD, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf) == 0);
    check_statxbuf(statxbuf, 4096);
    REQUIRE(unlinkat(AT_FDCWD, PATHNAME, AT_REMOVEDIR) != -1);
    REQUIRE(faccessat(AT_FDCWD, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test statx syscall on file in a different directory using absolute path", "[syscall]") {
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
    struct statx statxbuf {};
    REQUIRE(statx(AT_FDCWD, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf) == 0);
    check_statxbuf(statxbuf, strlen(BUFFER) * sizeof(char));
    REQUIRE(unlinkat(0, PATHNAME, 0) != -1);
    REQUIRE(faccessat(0, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test statx syscall on folder in a different directory using absolute path",
          "[syscall]") {
    const auto path_fs = std::filesystem::path(std::getenv("PWD")) / std::filesystem::path("test");
    const char *PATHNAME = path_fs.c_str();
    REQUIRE(mkdirat(0, PATHNAME, S_IRWXU) != -1);
    REQUIRE(faccessat(0, PATHNAME, F_OK, 0) == 0);
    struct statx statxbuf {};
    REQUIRE(statx(AT_FDCWD, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf) == 0);
    check_statxbuf(statxbuf, 4096);
    REQUIRE(unlinkat(0, PATHNAME, AT_REMOVEDIR) != -1);
    REQUIRE(faccessat(0, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test statx syscall on file in a different directory using dirfd", "[syscall]") {
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
    struct statx statxbuf {};
    REQUIRE(statx(AT_FDCWD, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf) == 0);
    check_statxbuf(statxbuf, strlen(BUFFER) * sizeof(char));
    REQUIRE(unlinkat(dirfd, PATHNAME, 0) != -1);
    REQUIRE(faccessat(dirfd, PATHNAME, F_OK, 0) != 0);
}

TEST_CASE("Test statx syscall on folder in a different directory using dirfd", "[syscall]") {
    constexpr const char *PATHNAME = "test";
    const char *DIRPATH            = std::getenv("PWD");
    int flags                      = O_RDONLY | O_DIRECTORY;
    int dirfd                      = open(DIRPATH, flags);
    REQUIRE(dirfd != -1);
    REQUIRE(mkdirat(dirfd, PATHNAME, S_IRWXU) != -1);
    REQUIRE(faccessat(dirfd, PATHNAME, F_OK, 0) == 0);
    struct statx statxbuf {};
    REQUIRE(statx(AT_FDCWD, PATHNAME, 0, STATX_BASIC_STATS | STATX_BTIME, &statxbuf) == 0);
    check_statxbuf(statxbuf, 4096);
    REQUIRE(unlinkat(dirfd, PATHNAME, AT_REMOVEDIR) != -1);
    REQUIRE(faccessat(dirfd, PATHNAME, F_OK, 0) != 0);
    REQUIRE(close(dirfd) != -1);
}
