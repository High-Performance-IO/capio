#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

TEST(SystemCallTest, TestDup) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(oldfd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(write(oldfd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    int newfd = dup(oldfd);
    EXPECT_GT(newfd, oldfd);
    EXPECT_EQ(lseek(oldfd, 0, SEEK_CUR), lseek(newfd, 0, SEEK_CUR));
    EXPECT_EQ(lseek(oldfd, 0, SEEK_SET), 0);
    EXPECT_EQ(lseek(oldfd, 0, SEEK_CUR), lseek(newfd, 0, SEEK_CUR));
    EXPECT_NE(close(oldfd), -1);
    EXPECT_NE(close(newfd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDup2) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(oldfd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(write(oldfd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    int newfd = oldfd + 10;
    EXPECT_EQ(dup2(oldfd, newfd), newfd);
    EXPECT_GT(newfd, oldfd);
    EXPECT_EQ(lseek(oldfd, 0, SEEK_CUR), lseek(newfd, 0, SEEK_CUR));
    EXPECT_EQ(lseek(oldfd, 0, SEEK_SET), 0);
    EXPECT_EQ(lseek(oldfd, 0, SEEK_CUR), lseek(newfd, 0, SEEK_CUR));
    EXPECT_NE(close(oldfd), -1);
    EXPECT_NE(close(newfd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDup2WithNewfdPointingToOpenFile) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(oldfd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(write(oldfd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    int newfd = open("/dev/null", O_WRONLY);
    EXPECT_EQ(dup2(oldfd, newfd), newfd);
    EXPECT_EQ(lseek(oldfd, 0, SEEK_CUR), lseek(newfd, 0, SEEK_CUR));
    EXPECT_NE(close(oldfd), -1);
    EXPECT_NE(close(newfd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDup2WithTwoEqualArguments) {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd                      = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(oldfd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    int newfd = oldfd;
    EXPECT_EQ(dup2(oldfd, newfd), newfd);
    EXPECT_NE(close(oldfd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDup3) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(oldfd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(write(oldfd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    int newfd = oldfd + 10;
    EXPECT_EQ(dup3(oldfd, newfd, 0), newfd);
    EXPECT_GT(newfd, oldfd);
    EXPECT_EQ(lseek(oldfd, 0, SEEK_CUR), lseek(newfd, 0, SEEK_CUR));
    EXPECT_EQ(lseek(oldfd, 0, SEEK_SET), 0);
    EXPECT_EQ(lseek(oldfd, 0, SEEK_CUR), lseek(newfd, 0, SEEK_CUR));
    EXPECT_NE(close(oldfd), -1);
    EXPECT_NE(close(newfd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDup3WithNewfdPointingToOpenFile) {
    constexpr const char *PATHNAME = "test_file.txt";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    int flags = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(oldfd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    EXPECT_EQ(write(oldfd, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    int newfd = open("/dev/null", O_WRONLY);
    EXPECT_EQ(dup3(oldfd, newfd, 0), newfd);
    EXPECT_EQ(lseek(oldfd, 0, SEEK_CUR), lseek(newfd, 0, SEEK_CUR));
    EXPECT_NE(close(oldfd), -1);
    EXPECT_NE(close(newfd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDup3WithTwoEqualArguments) {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd                      = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(oldfd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    int newfd = oldfd;
    EXPECT_EQ(dup3(oldfd, newfd, 0), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_NE(close(oldfd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDup3WithOCloexec) {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_WRONLY | O_TRUNC;
    int oldfd                      = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(oldfd, -1);
    EXPECT_EQ(access(PATHNAME, F_OK), 0);
    int newfd = oldfd + 10;
    EXPECT_EQ(dup3(oldfd, newfd, O_CLOEXEC), newfd);
    EXPECT_NE((fcntl(oldfd, F_GETFD) & FD_CLOEXEC), FD_CLOEXEC);
    EXPECT_EQ((fcntl(newfd, F_GETFD) & FD_CLOEXEC), FD_CLOEXEC);
    EXPECT_NE(close(oldfd), -1);
    EXPECT_NE(close(newfd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
    EXPECT_NE(access(PATHNAME, F_OK), 0);
}