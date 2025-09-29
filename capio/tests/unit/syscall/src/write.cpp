#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string_view>

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

TEST(SystemCallTest, TestFileCreateWriteClose) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int fd = open(PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(write(fd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    EXPECT_NE(close(fd), -1);
    fd = open(PATHNAME, O_RDONLY, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    std::unique_ptr<char[]> buf(new char[strlen(BUFFER)]);
    EXPECT_EQ(read(fd, buf.get(), strlen(BUFFER)), strlen(BUFFER));
    buf[strlen(BUFFER)] = '\0';
    EXPECT_EQ(std::string_view(BUFFER), std::string_view(buf.get()));
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestFileCreateWriteLseekClose) {
    constexpr const char *PATHNAME       = "test_file.txt";
    constexpr std::array<int, 12> BUFFER = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(write(fd, BUFFER.data(), 3 * sizeof(int)), 3 * sizeof(int));
    EXPECT_EQ(lseek(fd, 3 * sizeof(int), SEEK_CUR), 2 * 3 * sizeof(int));
    EXPECT_EQ(write(fd, BUFFER.data() + 6, 3 * sizeof(int)), 3 * sizeof(int));
    EXPECT_EQ(lseek(fd, 3 * sizeof(int), SEEK_SET), 3 * sizeof(int));
    EXPECT_EQ(write(fd, BUFFER.data() + 3, 3 * sizeof(int)), 3 * sizeof(int));
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    std::array<int, 12> buf{};
    EXPECT_EQ(read(fd, buf.data(), 3 * sizeof(int)), 3 * sizeof(int));
    EXPECT_EQ(lseek(fd, 3 * sizeof(int), SEEK_CUR), 2 * 3 * sizeof(int));
    EXPECT_EQ(read(fd, buf.data() + 6, 3 * sizeof(int)), 3 * sizeof(int));
    EXPECT_EQ(lseek(fd, 3 * sizeof(int), SEEK_SET), 3 * sizeof(int));
    EXPECT_EQ(read(fd, buf.data() + 3, 3 * sizeof(int)), 3 * sizeof(int));
    EXPECT_EQ(BUFFER, buf);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestFileCreateBufferedWriteClose) {
    constexpr const char *PATHNAME              = "test_file.txt";
    constexpr const std::array<int, 10> BUFFER1 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    constexpr const std::array<int, 8> BUFFER2  = {10, 11, 12, 13, 14, 15, 16, 17};
    constexpr const std::array<int, 2> BUFFER3  = {18, 19};
    constexpr const int TOTAL_SIZE =
        BUFFER1.size() * sizeof(int) + BUFFER2.size() * sizeof(int) + BUFFER3.size() * sizeof(int);

    struct iovec iov_write[3];
    iov_write[0].iov_base = const_cast<int *>(BUFFER1.data());
    iov_write[0].iov_len  = BUFFER1.size() * sizeof(int);
    iov_write[1].iov_base = const_cast<int *>(BUFFER2.data());
    iov_write[1].iov_len  = BUFFER2.size() * sizeof(int);
    iov_write[2].iov_base = const_cast<int *>(BUFFER3.data());
    iov_write[2].iov_len  = BUFFER3.size() * sizeof(int);

    int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(writev(fd, iov_write, 3), TOTAL_SIZE);
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);

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

    EXPECT_EQ(readv(fd, iov_read, 3), TOTAL_SIZE);
    EXPECT_EQ(BUFFER1, buf1);
    EXPECT_EQ(BUFFER2, buf2);
    EXPECT_EQ(BUFFER3, buf3);

    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}