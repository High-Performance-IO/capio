#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

TEST(SystemCallTest, TestFileRenameWhenNewPathDoesNotExist) {
    constexpr const char *OLDNAME = "test_file.txt";
    constexpr const char *NEWNAME = "test_file2.txt";
    int flags                     = O_CREAT | O_WRONLY | O_TRUNC;
    int fd                        = open(OLDNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(close(fd), -1);
    EXPECT_EQ(access(OLDNAME, F_OK), 0);
    EXPECT_NE(access(NEWNAME, F_OK), 0);
    EXPECT_EQ(rename(OLDNAME, NEWNAME), 0);
    EXPECT_NE(access(OLDNAME, F_OK), 0);
    EXPECT_EQ(access(NEWNAME, F_OK), 0);
    EXPECT_NE(unlink(NEWNAME), -1);
    EXPECT_NE(access(NEWNAME, F_OK), 0);
}

TEST(SystemCallTest, TestFileRenameWithNewPathAlreadyExists) {
    constexpr const char *OLDNAME = "test_file.txt";
    constexpr const char *NEWNAME = "test_file2.txt";
    int flags                     = O_CREAT | O_WRONLY | O_TRUNC;
    int fd                        = open(OLDNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(close(fd), -1);
    fd = open(NEWNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(close(fd), -1);
    EXPECT_EQ(access(OLDNAME, F_OK), 0);
    EXPECT_EQ(access(NEWNAME, F_OK), 0);
    EXPECT_EQ(rename(OLDNAME, NEWNAME), 0);
    EXPECT_NE(access(OLDNAME, F_OK), 0);
    EXPECT_EQ(access(NEWNAME, F_OK), 0);
    EXPECT_NE(unlink(NEWNAME), -1);
    EXPECT_NE(access(NEWNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDirectoryRenameWhenNewPathDoesNotExist) {
    constexpr const char *OLDNAME = "test";
    constexpr const char *NEWNAME = "test2";
    EXPECT_NE(mkdir(OLDNAME, S_IRWXU), -1);
    EXPECT_EQ(access(OLDNAME, F_OK), 0);
    EXPECT_NE(access(NEWNAME, F_OK), 0);
    EXPECT_EQ(rename(OLDNAME, NEWNAME), 0);
    EXPECT_NE(access(OLDNAME, F_OK), 0);
    EXPECT_EQ(access(NEWNAME, F_OK), 0);
    EXPECT_NE(rmdir(NEWNAME), -1);
    EXPECT_NE(access(NEWNAME, F_OK), 0);
}

TEST(SystemCallTest, TestDirectoryRenameWhenNewPathAlreadyExists) {
    constexpr const char *OLDNAME = "test";
    constexpr const char *NEWNAME = "test2";
    EXPECT_NE(mkdir(OLDNAME, S_IRWXU), -1);
    EXPECT_NE(mkdir(NEWNAME, S_IRWXU), -1);
    EXPECT_EQ(access(OLDNAME, F_OK), 0);
    EXPECT_EQ(access(NEWNAME, F_OK), 0);
    EXPECT_EQ(rename(OLDNAME, NEWNAME), 0);
    EXPECT_NE(access(OLDNAME, F_OK), 0);
    EXPECT_EQ(access(NEWNAME, F_OK), 0);
    EXPECT_NE(rmdir(NEWNAME), -1);
    EXPECT_NE(access(NEWNAME, F_OK), 0);
}
