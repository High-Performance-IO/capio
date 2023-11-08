#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <unistd.h>

TEST_CASE("Test dup system call", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(oldfd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(write(oldfd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    int newfd = dup(oldfd);
    REQUIRE(newfd > oldfd);
    REQUIRE(lseek(oldfd, 0, SEEK_CUR) == lseek(newfd, 0, SEEK_CUR));
    REQUIRE(lseek(oldfd, 0, SEEK_SET) == 0);
    REQUIRE(lseek(oldfd, 0, SEEK_CUR) == lseek(newfd, 0, SEEK_CUR));
    REQUIRE(close(oldfd) != -1);
    REQUIRE(close(newfd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test dup2 system call", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(oldfd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(write(oldfd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    int newfd = oldfd + 10;
    REQUIRE(dup2(oldfd, newfd) == newfd);
    REQUIRE(newfd > oldfd);
    REQUIRE(lseek(oldfd, 0, SEEK_CUR) == lseek(newfd, 0, SEEK_CUR));
    REQUIRE(lseek(oldfd, 0, SEEK_SET) == 0);
    REQUIRE(lseek(oldfd, 0, SEEK_CUR) == lseek(newfd, 0, SEEK_CUR));
    REQUIRE(close(oldfd) != -1);
    REQUIRE(close(newfd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test dup2 system call when newfd points to an open file", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(oldfd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(write(oldfd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    int newfd = open("/dev/null", O_WRONLY);
    REQUIRE(dup2(oldfd, newfd) == newfd);
    REQUIRE(lseek(oldfd, 0, SEEK_CUR) == lseek(newfd, 0, SEEK_CUR));
    REQUIRE(close(oldfd) != -1);
    REQUIRE(close(newfd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test dup2 system call when the two arguments are equal", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd                      = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(oldfd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    int newfd = oldfd;
    REQUIRE(dup2(oldfd, newfd) == newfd);
    REQUIRE(close(oldfd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test dup3 system call", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(oldfd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(write(oldfd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    int newfd = oldfd + 10;
    REQUIRE(dup3(oldfd, newfd, 0) == newfd);
    REQUIRE(newfd > oldfd);
    REQUIRE(lseek(oldfd, 0, SEEK_CUR) == lseek(newfd, 0, SEEK_CUR));
    REQUIRE(lseek(oldfd, 0, SEEK_SET) == 0);
    REQUIRE(lseek(oldfd, 0, SEEK_CUR) == lseek(newfd, 0, SEEK_CUR));
    REQUIRE(close(oldfd) != -1);
    REQUIRE(close(newfd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test dup3 system call when newfd points to an open file", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(oldfd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(write(oldfd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    int newfd = open("/dev/null", O_WRONLY);
    REQUIRE(dup3(oldfd, newfd, 0) == newfd);
    REQUIRE(lseek(oldfd, 0, SEEK_CUR) == lseek(newfd, 0, SEEK_CUR));
    REQUIRE(close(oldfd) != -1);
    REQUIRE(close(newfd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test dup3 system call when the two arguments are equal", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd                      = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(oldfd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    int newfd = oldfd;
    REQUIRE(dup3(oldfd, newfd, 0) == -1);
    REQUIRE(errno == EINVAL);
    REQUIRE(close(oldfd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test dup3 system call with the O_CLOEXEC flag", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd                      = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(oldfd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    int newfd = oldfd + 10;
    REQUIRE(dup3(oldfd, newfd, O_CLOEXEC) == newfd);
    REQUIRE((fcntl(oldfd, F_GETFD) & FD_CLOEXEC) != FD_CLOEXEC);
    REQUIRE((fcntl(newfd, F_GETFD) & FD_CLOEXEC) == FD_CLOEXEC);
    REQUIRE(close(oldfd) != -1);
    REQUIRE(close(newfd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}