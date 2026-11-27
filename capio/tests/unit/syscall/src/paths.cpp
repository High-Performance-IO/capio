#include <gtest/gtest.h>

#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "helpers.hpp"

/*
 * The forms of the calls on paths that the other tests leave out: the older system calls, which
 * glibc no longer makes on x86_64 but a program can, and the ones relative to a descriptor. A
 * missing file is "aNonExistingFile", which the configuration of the tests leaves out of CAPIO:
 * CAPIO waits for any other one to be created.
 */

#ifdef SYS_stat
TEST(SystemCallTest, TestTheStatAndLstatSystemCalls) {
    write_file("paths_stat", "0123456789");
    struct stat statbuf{};
    EXPECT_EQ(syscall(SYS_stat, "paths_stat", &statbuf), 0);
    EXPECT_EQ(statbuf.st_size, 10);
    EXPECT_TRUE(S_ISREG(statbuf.st_mode));
    EXPECT_EQ(syscall(SYS_lstat, "paths_stat", &statbuf), 0);
    EXPECT_EQ(statbuf.st_size, 10);

    EXPECT_EQ(syscall(SYS_stat, "aNonExistingFile", &statbuf), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(syscall(SYS_lstat, "aNonExistingFile", &statbuf), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(syscall(SYS_stat, nullptr, &statbuf), -1);
    EXPECT_EQ(errno, EFAULT);
    EXPECT_EQ(syscall(SYS_lstat, "paths_stat", nullptr), -1);
    EXPECT_EQ(errno, EFAULT);
    EXPECT_EQ(syscall(SYS_stat, "paths_stat", nullptr), -1);
    EXPECT_EQ(errno, EFAULT);
    const int fd = open("paths_stat", O_RDONLY);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(syscall(SYS_fstat, fd, nullptr), -1);
    EXPECT_EQ(errno, EFAULT);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("paths_stat"), 0);
}
#endif

#ifdef SYS_mkdir
TEST(SystemCallTest, TestTheMkdirAndRmdirSystemCalls) {
    EXPECT_EQ(syscall(SYS_mkdir, "paths_dir", S_IRWXU), 0);
    struct stat statbuf{};
    EXPECT_EQ(stat("paths_dir", &statbuf), 0);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));
    EXPECT_EQ(syscall(SYS_mkdir, "paths_dir", S_IRWXU), -1);
    EXPECT_EQ(errno, EEXIST);
    EXPECT_EQ(syscall(SYS_rmdir, "paths_dir"), 0);
    EXPECT_EQ(syscall(SYS_rmdir, "paths_dir"), -1);
    EXPECT_EQ(errno, ENOENT);
}
#endif

TEST(SystemCallTest, TestSeekDataAndSeekHoleInAFileWithNoHoles) {
    write_file("paths_seek", "0123456789");
    const int fd = open("paths_seek", O_RDONLY);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(lseek(fd, 0, SEEK_DATA), 0);
    EXPECT_EQ(lseek(fd, 4, SEEK_DATA), 4);
    EXPECT_EQ(lseek(fd, 0, SEEK_HOLE), 10); // the end of a file is a hole
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 10);
    EXPECT_EQ(lseek(fd, 10, SEEK_DATA), -1); // no data at or past the end
    EXPECT_EQ(errno, ENXIO);
    EXPECT_EQ(lseek(fd, 10, SEEK_HOLE), -1);
    EXPECT_EQ(errno, ENXIO);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 10); // a failed seek does not move the offset
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("paths_seek"), 0);
}

TEST(SystemCallTest, TestFstatatWithAtEmptyPathAndAtFdcwdIsTheWorkingDirectory) {
    struct stat statbuf{};
    EXPECT_EQ(fstatat(AT_FDCWD, "", &statbuf, AT_EMPTY_PATH), 0);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));
}

TEST(SystemCallTest, TestCallsRelativeToADescriptorOfAFileFailWithNotADirectory) {
    write_file("paths_notdir", "AB");
    const int fd = open("paths_notdir", O_RDONLY);
    ASSERT_NE(fd, -1);
    struct stat statbuf{};
    EXPECT_EQ(fstatat(fd, "child", &statbuf, 0), -1);
    EXPECT_EQ(errno, ENOTDIR);
    EXPECT_EQ(mkdirat(fd, "child", S_IRWXU), -1);
    EXPECT_EQ(errno, ENOTDIR);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("paths_notdir"), 0);
}

TEST(SystemCallTest, TestChdirAndFchdirIntoADirectoryOfCapioDir) {
    const std::string before = get_current_dir_name();
    EXPECT_EQ(mkdir("paths_cwd", S_IRWXU), 0);
    write_file("paths_cwd/file", "AB");

    EXPECT_EQ(chdir("paths_cwd"), 0);
    EXPECT_EQ(read_file("file"), "AB");
    EXPECT_EQ(chdir(".."), 0);

    const int fd = open("paths_cwd", O_RDONLY | O_DIRECTORY);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(fchdir(fd), 0);
    EXPECT_EQ(read_file("file"), "AB");
    EXPECT_EQ(chdir(before.c_str()), 0);
    EXPECT_EQ(close(fd), 0);

    EXPECT_EQ(chdir("aNonExistingFile"), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(unlink("paths_cwd/file"), 0);
    EXPECT_EQ(rmdir("paths_cwd"), 0);
}

TEST(SystemCallTest, TestChmodAndChownRelativeToADirectory) {
    if (!memory_variant() && std::getenv("LD_PRELOAD") != nullptr) {
        GTEST_SKIP() << "the FS variant does not implement chmod and chown of CAPIO files yet";
    }
    EXPECT_EQ(mkdir("paths_mode", S_IRWXU), 0);
    write_file("paths_mode/file", "AB");
    const int dirfd = open("paths_mode", O_RDONLY | O_DIRECTORY);
    ASSERT_NE(dirfd, -1);
    const int fd = open("paths_mode/file", O_RDONLY);
    ASSERT_NE(fd, -1);

    EXPECT_EQ(fchmodat(dirfd, "file", S_IRUSR | S_IWUSR, 0), 0);
    EXPECT_EQ(fchownat(dirfd, "file", getuid(), getgid(), 0), 0);
    EXPECT_EQ(fchownat(fd, "", getuid(), getgid(), AT_EMPTY_PATH), 0);
    EXPECT_EQ(fchownat(dirfd, "missing", getuid(), getgid(), 0), -1);
    EXPECT_EQ(errno, ENOENT);

    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(close(dirfd), 0);
    EXPECT_EQ(unlink("paths_mode/file"), 0);
    EXPECT_EQ(rmdir("paths_mode"), 0);
}
