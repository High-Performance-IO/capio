#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string_view>

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

TEST_CASE("Test file creation, write and close", "[syscall]") {
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

TEST_CASE("Test file creation, buffered write and close", "[syscall]") {
    constexpr const char *PATHNAME              = "test_file.txt";
    constexpr const std::array<int, 10> BUFFER1 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    constexpr const std::array<int, 8> BUFFER2  = {10, 11, 12, 13, 14, 15, 16, 17};
    constexpr const std::array<int, 2> BUFFER3  = {18, 19};

    struct iovec iov_write[3];
    iov_write[0].iov_base = const_cast<int *>(BUFFER1.data());
    iov_write[0].iov_len  = BUFFER1.size() * sizeof(int);
    iov_write[1].iov_base = const_cast<int *>(BUFFER2.data());
    iov_write[1].iov_len  = BUFFER2.size() * sizeof(int);
    iov_write[2].iov_base = const_cast<int *>(BUFFER3.data());
    iov_write[2].iov_len  = BUFFER3.size() * sizeof(int);

    int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    REQUIRE(access(PATHNAME, F_OK) == 0);
    REQUIRE(writev(fd, iov_write, 3));
    REQUIRE(lseek(fd, 0, SEEK_SET) == 0);

    std::array<int, 10> buf1{};
    std::array<int, 8> buf2{};
    std::array<int, 2> buf3{};

    struct iovec iov_read[3];
    iov_read[0].iov_base = const_cast<int *>(buf1.data());
    iov_read[0].iov_len  = buf1.size() * sizeof(int);
    iov_read[1].iov_base = const_cast<int *>(buf2.data());
    iov_read[1].iov_len  = buf2.size() * sizeof(int);
    iov_read[2].iov_base = const_cast<int *>(buf3.data());
    iov_read[2].iov_len  = buf3.size() * sizeof(int);

    REQUIRE(readv(fd, iov_read, 3));
    REQUIRE(BUFFER1 == buf1);
    REQUIRE(BUFFER2 == buf2);
    REQUIRE(BUFFER3 == buf3);

    REQUIRE(close(fd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
    REQUIRE(access(PATHNAME, F_OK) != 0);
}