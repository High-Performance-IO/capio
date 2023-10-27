#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string_view>

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

TEST_CASE("Test file creation, write and close", "[posix]") {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = open(PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(write(fd, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(fd) != -1);
    fd = open(PATHNAME, O_RDONLY, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    std::unique_ptr<char[]> buf(new char[strlen(BUFFER)]);
    REQUIRE(read(fd, buf.get(), strlen(BUFFER)) == strlen(BUFFER));
    buf[strlen(BUFFER)] = '\0';
    REQUIRE(std::string_view(BUFFER) == std::string_view(buf.get()));
    REQUIRE(close(fd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test file creation, write with lseek and close") {
    constexpr const char *PATHNAME       = "test_file.txt";
    constexpr std::array<int, 12> BUFFER = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(write(fd, BUFFER.data(), 3 * sizeof(int)) == 3 * sizeof(int));
    REQUIRE(lseek(fd, 3 * sizeof(int), SEEK_CUR) == 2 * 3 * sizeof(int));
    REQUIRE(write(fd, BUFFER.data() + 6, 3 * sizeof(int)) == 3 * sizeof(int));
    REQUIRE(lseek(fd, 3 * sizeof(int), SEEK_SET) == 3 * sizeof(int));
    REQUIRE(write(fd, BUFFER.data() + 3, 3 * sizeof(int)) == 3 * sizeof(int));
    REQUIRE(lseek(fd, 0, SEEK_SET) == 0);
    std::array<int, 12> buf{};
    REQUIRE(read(fd, buf.data(), 3 * sizeof(int)) == 3 * sizeof(int));
    REQUIRE(lseek(fd, 3 * sizeof(int), SEEK_CUR) == 2 * 3 * sizeof(int));
    REQUIRE(read(fd, buf.data() + 6, 3 * sizeof(int)) == 3 * sizeof(int));
    REQUIRE(lseek(fd, 3 * sizeof(int), SEEK_SET) == 3 * sizeof(int));
    REQUIRE(read(fd, buf.data() + 3, 3 * sizeof(int)) == 3 * sizeof(int));
    REQUIRE(BUFFER == buf);
    REQUIRE(close(fd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}

TEST_CASE("Test file creation, buffered write and close", "[posix]") {
    constexpr const char *PATHNAME              = "test_file.txt";
    constexpr const std::array<int, 10> BUFFER1 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    constexpr const std::array<int, 8> BUFFER2  = {10, 11, 12, 13, 14, 15, 16, 17};
    constexpr const std::array<int, 2> BUFFER3  = {18, 19};

    struct iovec iov[3];
    iov[0].iov_base = const_cast<int *>(BUFFER1.data());
    iov[0].iov_len  = BUFFER1.size() * sizeof(int);
    iov[1].iov_base = const_cast<int *>(BUFFER2.data());
    iov[1].iov_len  = BUFFER2.size() * sizeof(int);
    iov[2].iov_base = const_cast<int *>(BUFFER3.data());
    iov[2].iov_len  = BUFFER3.size() * sizeof(int);

    int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(writev(fd, iov, 3));
    REQUIRE(close(fd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}