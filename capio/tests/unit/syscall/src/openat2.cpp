#include <gtest/gtest.h>

#include <cstring>
#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

// openat2 is what tar 1.35 uses to open the directories it extracts into, with RESOLVE_BENEATH.

static int capio_test_openat2(int dirfd, const char *path, uint64_t flags, uint64_t mode,
                              uint64_t resolve) {
    struct open_how how {};
    how.flags   = flags;
    how.mode    = mode;
    how.resolve = resolve;
    return static_cast<int>(syscall(SYS_openat2, dirfd, path, &how, sizeof(how)));
}

TEST(SystemCallTest, TestOpenat2CreatesAndReopensAFile) {
    auto fd = capio_test_openat2(AT_FDCWD, "openat2.txt", O_CREAT | O_RDWR, 0644, 0);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(write(fd, "abcd", 4), 4);
    EXPECT_EQ(close(fd), 0);

    fd = capio_test_openat2(AT_FDCWD, "openat2.txt", O_RDONLY, 0, 0);
    ASSERT_GE(fd, 0);
    char buf[4];
    EXPECT_EQ(read(fd, buf, 4), 4);
    EXPECT_EQ(memcmp(buf, "abcd", 4), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("openat2.txt"), 0);
}

TEST(SystemCallTest, TestOpenat2BeneathOpensADirectoryCreatedUnderCapio) {
    EXPECT_EQ(mkdir("openat2_dir", 0755), 0);
    // What tar does before it creates a file in the directory.
    auto dirfd = capio_test_openat2(AT_FDCWD, "openat2_dir/",
                                    O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_PATH | O_DIRECTORY, 0,
                                    RESOLVE_BENEATH);
    ASSERT_GE(dirfd, 0);

    auto fd = openat(dirfd, "f", O_CREAT | O_WRONLY | O_EXCL, 0644);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(write(fd, "abcd", 4), 4);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(close(dirfd), 0);

    struct stat statbuf {};
    EXPECT_EQ(stat("openat2_dir/f", &statbuf), 0);
    EXPECT_EQ(statbuf.st_size, 4);

    EXPECT_EQ(unlink("openat2_dir/f"), 0);
    EXPECT_NE(rmdir("openat2_dir"), -1);
}

TEST(SystemCallTest, TestOpenat2BeneathRefusesToLeaveTheDirectory) {
    auto fd = capio_test_openat2(AT_FDCWD, "openat2.txt", O_CREAT | O_RDWR, 0644, 0);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(close(fd), 0);

    EXPECT_EQ(capio_test_openat2(AT_FDCWD, "/etc/hostname", O_RDONLY, 0, RESOLVE_BENEATH), -1);
    EXPECT_EQ(errno, EXDEV);
    EXPECT_EQ(capio_test_openat2(AT_FDCWD, "../openat2.txt", O_RDONLY, 0, RESOLVE_BENEATH), -1);
    EXPECT_EQ(errno, EXDEV);

    // ".." that stays inside the directory is fine.
    fd = capio_test_openat2(AT_FDCWD, "x/../openat2.txt", O_RDONLY, 0, RESOLVE_BENEATH);
    EXPECT_GE(fd, 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("openat2.txt"), 0);
}

TEST(SystemCallTest, TestOpenat2InRootStopsDotDotAtTheRoot) {
    auto fd = capio_test_openat2(AT_FDCWD, "openat2.txt", O_CREAT | O_RDWR, 0644, 0);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(write(fd, "abcd", 4), 4);
    EXPECT_EQ(close(fd), 0);

    fd = capio_test_openat2(AT_FDCWD, "../../openat2.txt", O_RDONLY, 0, RESOLVE_IN_ROOT);
    ASSERT_GE(fd, 0);
    char buf[4];
    EXPECT_EQ(read(fd, buf, 4), 4);
    EXPECT_EQ(memcmp(buf, "abcd", 4), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("openat2.txt"), 0);
}

TEST(SystemCallTest, TestOpenat2RejectsUnknownResolveFlags) {
    EXPECT_EQ(capio_test_openat2(AT_FDCWD, "openat2.txt", O_RDONLY, 0, 1ULL << 40), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(
        capio_test_openat2(AT_FDCWD, "openat2.txt", O_RDONLY, 0, RESOLVE_BENEATH | RESOLVE_IN_ROOT),
        -1);
    EXPECT_EQ(errno, EINVAL);
}
