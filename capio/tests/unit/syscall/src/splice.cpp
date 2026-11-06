#include <gtest/gtest.h>

#include <cstring>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <unistd.h>

// splice and sendfile would move the data past CAPIO, so they fail on a CAPIO file and copy tools
// fall back to read and write. copy_file_range is done by CAPIO: see copy_file_range.cpp.

TEST(SystemCallTest, TestSpliceIntoACapioFileFailsWithEinval) {
    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);
    EXPECT_EQ(write(pipe_fds[1], "abcd", 4), 4);
    auto fd = open("test.txt", O_CREAT | O_WRONLY, 0777);
    EXPECT_EQ(splice(pipe_fds[0], nullptr, fd, nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EINVAL);

    // The data stays in the pipe, for the caller to write with write().
    char buf[4];
    EXPECT_EQ(read(pipe_fds[0], buf, 4), 4);
    EXPECT_EQ(memcmp(buf, "abcd", 4), 0);

    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(close(pipe_fds[0]), 0);
    EXPECT_EQ(close(pipe_fds[1]), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestSpliceFromACapioFileFailsWithEinval) {
    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(write(fd, "abcd", 4), 4);
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    EXPECT_EQ(splice(fd, nullptr, pipe_fds[1], nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(close(pipe_fds[0]), 0);
    EXPECT_EQ(close(pipe_fds[1]), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestSendfileFromACapioFileFailsWithEinval) {
    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(write(fd, "abcd", 4), 4);
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    EXPECT_EQ(sendfile(pipe_fds[1], fd, nullptr, 4), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(close(pipe_fds[0]), 0);
    EXPECT_EQ(close(pipe_fds[1]), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestSpliceBetweenPipesStillWorks) {
    int a[2], b[2];
    EXPECT_EQ(pipe(a), 0);
    EXPECT_EQ(pipe(b), 0);
    EXPECT_EQ(write(a[1], "abcd", 4), 4);
    EXPECT_EQ(splice(a[0], nullptr, b[1], nullptr, 4, 0), 4);
    char buf[4];
    EXPECT_EQ(read(b[0], buf, 4), 4);
    EXPECT_EQ(memcmp(buf, "abcd", 4), 0);
    for (int fd : {a[0], a[1], b[0], b[1]}) {
        EXPECT_EQ(close(fd), 0);
    }
}
