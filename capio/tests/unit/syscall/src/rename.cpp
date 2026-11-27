#include <gtest/gtest.h>

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "helpers.hpp"

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

TEST(SystemCallTest, TestRenameOfAFileToItselfDoesNothing) {
    write_file("rename_same", "AB");
    EXPECT_EQ(rename("rename_same", "rename_same"), 0);
    EXPECT_EQ(read_file("rename_same"), "AB");
    EXPECT_EQ(unlink("rename_same"), 0);
}

TEST(SystemCallTest, TestRenameOfAMissingFileFails) {
    EXPECT_EQ(rename("rename_missing", "rename_missing2"), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(rename("rename_missing", "rename_missing"), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST(SystemCallTest, TestRenameOfAFileUnderItselfFailsWithNotADirectory) {
    write_file("rename_file", "AB");
    EXPECT_EQ(rename("rename_file", "rename_file/sub"), -1);
    EXPECT_EQ(errno, ENOTDIR);
    EXPECT_EQ(read_file("rename_file"), "AB");
    EXPECT_EQ(unlink("rename_file"), 0);
}

TEST(SystemCallTest, TestRenameOfADirectoryIntoItselfFails) {
    ASSERT_EQ(mkdir("rename_dir", S_IRWXU), 0);
    EXPECT_EQ(rename("rename_dir", "rename_dir/sub"), -1);
    EXPECT_EQ(errno, EINVAL);
    ASSERT_EQ(mkdir("rename_dir/sub", S_IRWXU), 0);
    EXPECT_EQ(rename("rename_dir", "rename_dir/sub"), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(rename("rename_dir", "rename_dir/sub/deeper"), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(access("rename_dir/sub", F_OK), 0);
    EXPECT_EQ(rmdir("rename_dir/sub"), 0);
    EXPECT_EQ(rmdir("rename_dir"), 0);
}

// "rename_a" is a prefix of "rename_ab" as a string, but not as a path
TEST(SystemCallTest, TestRenameToANameThatStartsWithTheOldOne) {
    ASSERT_EQ(mkdir("rename_a", S_IRWXU), 0);
    EXPECT_EQ(rename("rename_a", "rename_ab"), 0);
    EXPECT_NE(access("rename_a", F_OK), 0);
    EXPECT_EQ(access("rename_ab", F_OK), 0);
    EXPECT_EQ(rmdir("rename_ab"), 0);
}

TEST(SystemCallTest, TestRenameNoReplaceKeepsAnExistingFile) {
    write_file("rename_nr1", "AB");
    write_file("rename_nr2", "CD");
    EXPECT_EQ(renameat2(AT_FDCWD, "rename_nr1", AT_FDCWD, "rename_nr2", RENAME_NOREPLACE), -1);
    EXPECT_EQ(errno, EEXIST);
    EXPECT_EQ(read_file("rename_nr1"), "AB");
    EXPECT_EQ(read_file("rename_nr2"), "CD");

    EXPECT_EQ(unlink("rename_nr2"), 0);
    EXPECT_EQ(renameat2(AT_FDCWD, "rename_nr1", AT_FDCWD, "rename_nr2", RENAME_NOREPLACE), 0);
    EXPECT_NE(access("rename_nr1", F_OK), 0);
    EXPECT_EQ(read_file("rename_nr2"), "AB");
    EXPECT_EQ(unlink("rename_nr2"), 0);
}

TEST(SystemCallTest, TestRenameExchangeSwapsTwoFiles) {
    write_file("rename_ex1", "AB");
    write_file("rename_ex2", "CD");
    if (memory_variant()) { // not supported, as on the file systems without it
        EXPECT_EQ(renameat2(AT_FDCWD, "rename_ex1", AT_FDCWD, "rename_ex2", RENAME_EXCHANGE), -1);
        EXPECT_EQ(errno, EINVAL);
        EXPECT_EQ(unlink("rename_ex1"), 0);
        EXPECT_EQ(unlink("rename_ex2"), 0);
        return;
    }
    const int fd = open("rename_ex1", O_RDONLY);
    ASSERT_NE(fd, -1);

    EXPECT_EQ(renameat2(AT_FDCWD, "rename_ex1", AT_FDCWD, "rename_ex2", RENAME_EXCHANGE), 0);
    EXPECT_EQ(read_file("rename_ex1"), "CD");
    EXPECT_EQ(read_file("rename_ex2"), "AB");
    char buffer[2]{};
    EXPECT_EQ(read(fd, buffer, sizeof(buffer)), 2); // the descriptor follows its file
    EXPECT_EQ(std::string(buffer, 2), "AB");
    EXPECT_EQ(close(fd), 0);

    EXPECT_EQ(unlink("rename_ex1"), 0);
    EXPECT_EQ(renameat2(AT_FDCWD, "rename_ex2", AT_FDCWD, "rename_ex1", RENAME_EXCHANGE), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(unlink("rename_ex2"), 0);
}

TEST(SystemCallTest, TestRenameWithInvalidFlagsFails) {
    write_file("rename_flags", "AB");
    EXPECT_EQ(renameat2(AT_FDCWD, "rename_flags", AT_FDCWD, "rename_flags2",
                        RENAME_NOREPLACE | RENAME_EXCHANGE),
              -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(renameat2(AT_FDCWD, "rename_flags", AT_FDCWD, "rename_flags2", 1U << 30), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(read_file("rename_flags"), "AB");
    EXPECT_EQ(unlink("rename_flags"), 0);
}

// Memory variant: the file is copied to its new name on disk, where CAPIO does not look for it,
// and the source stays. Disabled: the open of the new name waits for a producer forever.
TEST(SystemCallTest, DISABLED_TestRenameFromOutsideIntoCapioDir) {
    char outside[] = "/tmp/capio_rename_XXXXXX";
    ASSERT_NE(mkdtemp(outside), nullptr);
    const std::string source = std::string(outside) + "/f";
    write_file(source.c_str(), "AB");
    EXPECT_EQ(rename(source.c_str(), "rename_in"), 0);
    EXPECT_NE(access(source.c_str(), F_OK), 0);
    EXPECT_EQ(read_file("rename_in"), "AB");
    EXPECT_EQ(unlink("rename_in"), 0);
    EXPECT_EQ(rmdir(outside), 0);
}
